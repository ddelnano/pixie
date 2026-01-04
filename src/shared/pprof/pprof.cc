/*
 * Copyright 2018- The Pixie Authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "src/shared/pprof/pprof.h"

#include <absl/strings/numbers.h>
#include <absl/strings/str_format.h>
#include <absl/strings/str_join.h>
#include <absl/strings/str_split.h>
#include <absl/strings/strip.h>

#include <vector>

#include "src/common/base/base.h"
#include "src/stirling/obj_tools/elf_reader.h"

namespace px {
namespace shared {

PProfProfile CreatePProfProfile(const uint32_t period_ms, const PProfHisto& histo) {
  // Info on the pprof proto format:
  // https://github.com/google/pprof/blob/main/proto/profile.proto

  // period_ms is the stack trace sampling period used by the eBPF stack trace sampling probe.
  // period_ns will be used when populating the nanos count.
  const uint64_t period_ns = period_ms * 1000 * 1000;

  // Tracks which strings have been inserted into the profile.
  absl::flat_hash_map<std::string, uint64_t> strings;

  // This is the pprof profile.
  ::perftools::profiles::Profile profile;

  // The pprof profiles start by describing their sample types. For CPU profiling,
  // the convention is to describe two different metrics:
  // "samples" with units of "count", and
  // "cpu" with units of "nanoseconds".
  // Note, the first entry in the strings table is required to be an empty string.
  auto sample_type = profile.add_sample_type();
  sample_type->set_type(1);
  sample_type->set_unit(2);
  profile.add_string_table("");
  profile.add_string_table("samples");
  profile.add_string_table("count");
  sample_type = profile.add_sample_type();
  sample_type->set_type(3);
  sample_type->set_unit(4);
  profile.add_string_table("cpu");
  profile.add_string_table("nanoseconds");

  // Store the underlying stack trace sampling period.
  auto period_type = profile.mutable_period_type();
  period_type->set_type(3);
  period_type->set_unit(4);
  profile.set_period(period_ns);

  // State variables useful as we build the pprof profile message.
  // No locations messages exist (yet); next_location_id starts at 1.
  // We already added some strings; next_string_id starts at 5.
  uint64_t next_location_id = 1;
  uint64_t next_string_id = 5;

  // To build the profile, we iterate over the stack traces histogram.
  for (const auto& [stack_trace_str, count] : histo) {
    // Each histo entry will be recorded as a new sample.
    auto sample = profile.add_sample();

    // That sample will record its count and time in nanos.
    const uint64_t nanos = count * period_ns;
    sample->add_value(count);
    sample->add_value(nanos);

    // Our stack traces are symbolized like so: main;foo;bar
    // thus, we split on ';' to iterate over the individual symbols.
    const std::vector<std::string_view> symbols = absl::StrSplit(stack_trace_str, ";");

    // Iterate over the symbols in this stack trace.
    // Each symbol will be added to the sample as a "location" message, which in turn refers to a
    // string in the strings table (tracked in map "strings") (more details below).
    // Note, because of how we built the stack trace string and how the pprof profile is
    // organized, we iterate in reverse order.
    for (auto symbols_iter = symbols.rbegin(); symbols_iter != symbols.rend(); ++symbols_iter) {
      const auto& symbol = *symbols_iter;

      // try_emplace() looks up an existing entry in the strings table, or creates a new entry
      // with the key provided. Here a new entry maps from symbol to next_location_id.
      const auto [strings_iter, inserted] = strings.try_emplace(symbol, next_location_id);

      if (inserted) {
        // New symbol: create a new location, add it to the sample, and create a new symbol.

        const uint64_t location_id = next_location_id;
        const uint64_t string_id = next_string_id;
        ++next_location_id;
        ++next_string_id;

        // Add a new "location" to the profile.
        // Each sample is a sequence of locations. The locations, in their sequence, represent a
        // stack trace. Each location may include an address and a reference to a mapping (useful
        // if symbols are not included in the profile, enables post-hoc symbolization).
        // Lacking both address and mapping here, we skip those.
        auto location = profile.add_location();
        location->set_id(location_id);

        // To connect a location to a symbol, we need to go through a "line" and a "function".

        // Add a "line" message to the location message.
        // In our usage, this is essentially a pointer to a function message that points to a
        // symbol. The function message may contain more information (e.g. starting line number).
        auto line = location->add_line();
        line->set_function_id(location_id);

        // Add a "function" to the profile (to point to the string table).
        auto function = profile.add_function();
        function->set_id(location_id);

        // Add a reference to the string table entry in the function message.
        function->set_name(string_id);

        // Add the string to the string table in the profile.
        profile.add_string_table(std::string(symbol));

        // Add the new location-id into the sample in the profile.
        sample->add_location_id(location_id);
      } else {
        // Existing symbol.
        // Just place the pre-existing location id into the sample.
        const uint64_t location_id = strings_iter->second;
        sample->add_location_id(location_id);
      }
    }
  }
  return profile;
}

absl::flat_hash_map<std::string, uint64_t> DeserializePProfProfile(const PProfProfile& pprof) {
  // This function reads from the protobuf pprof to populate and return this stack trace histogram.
  absl::flat_hash_map<std::string, uint64_t> histo;

  // Iterate over each sample to find the underlying stack trace string and its count.
  for (const auto& sample : pprof.sample()) {
    // Collect symbols into this vector.
    std::vector<std::string> symbols;

    // Iterate through the symbols, i.e. the stack trace locations. Two notes...
    // 1. Our stack trace strings (e.g. "main;compute;leaf") are in reversed in order vs. pprof.
    // 2. PProf proto locations are 1 indexed but C++ is 0 indexed, hence the "-1" offset applied to
    // the indices below. With 0 indexing, `go tool pprof` refused to render the pprof with
    // the following message: malformed profile: found function with reserved ID=0.
    auto& location_ids = sample.location_id();
    for (auto id_iter = location_ids.rbegin(); id_iter != location_ids.rend(); id_iter++) {
      const auto& location = pprof.location(*id_iter - 1);

      // Each location points to a line, which points to a function, which points to a symbol.
      DCHECK_EQ(location.line_size(), 1);
      const auto& line = location.line(0);
      const auto& function = pprof.function(line.function_id() - 1);
      const auto& symbol = pprof.string_table(function.name());

      // Found a symbol; store it.
      symbols.push_back(symbol);
    }

    // There are two values for each sample: count & nanos. Here, we ignore the nanos value because
    // it cannot be stored in the histogram that this function produces.
    DCHECK_EQ(sample.value_size(), 2);
    const uint64_t count = sample.value(0);
    const std::string stack_trace = absl::StrJoin(symbols, ";");
    histo[stack_trace] = count;
  }
  return histo;
}

StatusOr<PProfProfile> ParseLegacyHeapProfile(const std::string& legacy_text) {
  // Parse gperftools legacy heap profile format.
  // Format:
  //   heap profile:    <count>: <bytes> [   <count>: <bytes>] @ growth
  //        <count>: <bytes> [     <count>: <bytes>] @ 0xaddr1 0xaddr2 ...
  //
  // Each line after the header represents a sample with allocations and stack trace addresses.

  PProfProfile profile;

  // Set up sample types for heap profiles: allocations (count) and space (bytes)
  // string_table[0] must be empty
  profile.add_string_table("");       // 0: empty (required)
  profile.add_string_table("alloc_objects");  // 1
  profile.add_string_table("count");  // 2
  profile.add_string_table("alloc_space");    // 3
  profile.add_string_table("bytes");  // 4

  auto* sample_type_count = profile.add_sample_type();
  sample_type_count->set_type(1);  // alloc_objects
  sample_type_count->set_unit(2);  // count

  auto* sample_type_bytes = profile.add_sample_type();
  sample_type_bytes->set_type(3);  // alloc_space
  sample_type_bytes->set_unit(4);  // bytes

  // Map from address to location_id for deduplication
  absl::flat_hash_map<uint64_t, uint64_t> address_to_location;
  uint64_t next_location_id = 1;

  std::vector<std::string_view> lines = absl::StrSplit(legacy_text, '\n');

  for (const auto& line : lines) {
    std::string_view trimmed = absl::StripAsciiWhitespace(line);
    if (trimmed.empty()) continue;

    // Skip header line (starts with "heap profile:")
    if (absl::StartsWith(trimmed, "heap profile:")) continue;

    // Find the @ separator that precedes the addresses
    size_t at_pos = trimmed.find('@');
    if (at_pos == std::string_view::npos) continue;

    // Parse count and bytes from before the @
    // Format: "     1: 23642112 [     1: 23642112] @"
    std::string_view stats_part = trimmed.substr(0, at_pos);
    std::string_view addrs_part = trimmed.substr(at_pos + 1);

    // Extract count and bytes (first pair before the brackets)
    std::vector<std::string_view> stats_tokens =
        absl::StrSplit(stats_part, absl::ByAnyChar(": []"), absl::SkipWhitespace());

    if (stats_tokens.size() < 2) continue;

    int64_t count = 0;
    int64_t bytes = 0;
    if (!absl::SimpleAtoi(stats_tokens[0], &count)) continue;
    if (!absl::SimpleAtoi(stats_tokens[1], &bytes)) continue;

    // Parse addresses from after the @
    std::vector<std::string_view> addr_tokens =
        absl::StrSplit(addrs_part, ' ', absl::SkipWhitespace());

    if (addr_tokens.empty()) continue;

    // Skip "growth" or "heap_v2/..." type markers
    if (!addr_tokens.empty() && !absl::StartsWith(addr_tokens[0], "0x")) {
      continue;
    }

    // Create a sample for this stack trace
    auto* sample = profile.add_sample();
    sample->add_value(count);
    sample->add_value(bytes);

    // Parse each address and create/reuse locations
    for (const auto& addr_str : addr_tokens) {
      if (!absl::StartsWith(addr_str, "0x")) continue;

      // Parse hex address (skip "0x" prefix)
      uint64_t addr = 0;
      if (!absl::SimpleHexAtoi(addr_str.substr(2), &addr)) continue;

      // Check if we already have a location for this address
      auto it = address_to_location.find(addr);
      if (it != address_to_location.end()) {
        sample->add_location_id(it->second);
      } else {
        // Create a new location for this address
        uint64_t location_id = next_location_id++;
        address_to_location[addr] = location_id;

        auto* location = profile.add_location();
        location->set_id(location_id);
        location->set_address(addr);

        sample->add_location_id(location_id);
      }
    }
  }

  return profile;
}

StatusOr<std::vector<MemoryMapping>> ParseProcMaps(const std::string& maps_content) {
  // Parse /proc/<pid>/maps format:
  // vmem_start-vmem_end perms offset dev inode pathname
  // 57ab3a2e7000-57ab3c8ad000 r--p 00000000 00:00 653129 /path/to/binary

  std::vector<MemoryMapping> mappings;
  std::vector<std::string_view> lines = absl::StrSplit(maps_content, '\n');

  for (const auto& line : lines) {
    std::string_view trimmed = absl::StripAsciiWhitespace(line);
    if (trimmed.empty()) continue;

    // Split by whitespace
    std::vector<std::string_view> tokens =
        absl::StrSplit(trimmed, absl::ByAnyChar(" \t"), absl::SkipWhitespace());

    if (tokens.size() < 5) continue;

    MemoryMapping mapping;

    // Parse address range (vmem_start-vmem_end)
    size_t dash_pos = tokens[0].find('-');
    if (dash_pos == std::string_view::npos) continue;

    std::string_view start_str = tokens[0].substr(0, dash_pos);
    std::string_view end_str = tokens[0].substr(dash_pos + 1);

    if (!absl::SimpleHexAtoi(start_str, &mapping.vmem_start)) continue;
    if (!absl::SimpleHexAtoi(end_str, &mapping.vmem_end)) continue;

    // Parse permissions
    mapping.permissions = std::string(tokens[1]);

    // Parse file offset (hex)
    if (!absl::SimpleHexAtoi(tokens[2], &mapping.file_offset)) continue;

    // Skip dev and inode (tokens[3] and tokens[4])

    // Parse pathname (may be empty or contain spaces, so take everything after inode)
    if (tokens.size() >= 6) {
      mapping.pathname = std::string(tokens[5]);
    }

    mappings.push_back(std::move(mapping));
  }

  return mappings;
}

Status SymbolizePProfProfile(PProfProfile* profile, const std::vector<MemoryMapping>& mappings) {
  // Build a cache of ElfReader instances per unique binary path
  absl::flat_hash_map<std::string, std::unique_ptr<stirling::obj_tools::ElfReader::Symbolizer>>
      symbolizers;
  absl::flat_hash_map<std::string, std::unique_ptr<stirling::obj_tools::ElfReader>> elf_readers;

  // Map from location_id to function_id for deduplication
  absl::flat_hash_map<uint64_t, uint64_t> location_to_function;

  // The string table starts with standard entries, find the next available index
  uint64_t next_string_id = profile->string_table_size();
  uint64_t next_function_id = profile->function_size() + 1;

  // Helper to add a string to the string table and return its index
  auto add_string = [&](const std::string& str) -> uint64_t {
    profile->add_string_table(str);
    return next_string_id++;
  };

  // Iterate through all locations and symbolize
  for (int i = 0; i < profile->location_size(); ++i) {
    auto* location = profile->mutable_location(i);
    uint64_t addr = location->address();

    if (addr == 0) continue;

    // Find the mapping that contains this address
    const MemoryMapping* found_mapping = nullptr;
    for (const auto& mapping : mappings) {
      if (mapping.Contains(addr)) {
        found_mapping = &mapping;
        break;
      }
    }

    if (found_mapping == nullptr) {
      // Address not in any mapping, skip
      continue;
    }

    // Skip non-file mappings (like [heap], [stack], anonymous)
    if (found_mapping->pathname.empty() || found_mapping->pathname[0] == '[') {
      continue;
    }

    // Get or create the ElfReader for this binary
    auto& elf_reader = elf_readers[found_mapping->pathname];
    auto& symbolizer = symbolizers[found_mapping->pathname];

    if (elf_reader == nullptr) {
      auto elf_result = stirling::obj_tools::ElfReader::Create(found_mapping->pathname);
      if (!elf_result.ok()) {
        // Skip this binary if we can't read it
        continue;
      }
      elf_reader = elf_result.ConsumeValueOrDie();

      auto sym_result = elf_reader->GetSymbolizer();
      if (sym_result.ok()) {
        symbolizer = sym_result.ConsumeValueOrDie();
      }
    }

    if (symbolizer == nullptr) {
      continue;
    }

    // Convert virtual address to binary address.
    // For non-PIE binaries (ET_EXEC), the binary address equals the runtime virtual address.
    // For PIE binaries (ET_DYN), the binary address is runtime_addr - vmem_start
    // (since PIE ELF segments typically have vaddr starting at 0).
    uint64_t binary_addr;
    if (elf_reader->ELFType() == ELFIO::ET_DYN) {
      // PIE binary or shared library: subtract the mapping base
      binary_addr = addr - found_mapping->vmem_start;
    } else {
      // Non-PIE executable: addresses are absolute
      binary_addr = addr;
    }

    // Look up the symbol
    std::string_view symbol = symbolizer->Lookup(binary_addr);

    // If the symbol is just a hex address, skip it
    if (absl::StartsWith(symbol, "0x")) {
      continue;
    }

    // Create a function entry for this symbol
    uint64_t function_id = next_function_id++;
    uint64_t name_id = add_string(std::string(symbol));

    auto* function = profile->add_function();
    function->set_id(function_id);
    function->set_name(name_id);
    function->set_system_name(name_id);

    // Add a line entry to the location pointing to this function
    auto* line = location->add_line();
    line->set_function_id(function_id);

    location_to_function[location->id()] = function_id;
  }

  return Status::OK();
}

StatusOr<std::string> PProfToFoldedStacks(const PProfProfile& profile, int value_index) {
  // Validate value_index
  if (profile.sample_type_size() == 0) {
    return error::InvalidArgument("Profile has no sample types");
  }
  if (value_index < 0 || value_index >= profile.sample_type_size()) {
    return error::InvalidArgument("value_index $0 out of range [0, $1)", value_index,
                                  profile.sample_type_size());
  }

  // Build a map from location_id to location for fast lookup
  absl::flat_hash_map<uint64_t, const ::perftools::profiles::Location*> location_map;
  for (const auto& location : profile.location()) {
    location_map[location.id()] = &location;
  }

  // Build a map from function_id to function for fast lookup
  absl::flat_hash_map<uint64_t, const ::perftools::profiles::Function*> function_map;
  for (const auto& function : profile.function()) {
    function_map[function.id()] = &function;
  }

  std::string result;

  for (const auto& sample : profile.sample()) {
    if (sample.value_size() <= value_index) {
      continue;
    }

    int64_t value = sample.value(value_index);
    if (value == 0) {
      continue;
    }

    // Build stack trace: reverse order to get root→leaf
    std::vector<std::string> frames;
    auto& location_ids = sample.location_id();

    for (auto it = location_ids.rbegin(); it != location_ids.rend(); ++it) {
      uint64_t loc_id = *it;
      auto loc_it = location_map.find(loc_id);
      if (loc_it == location_map.end()) {
        // Location not found, use hex address placeholder
        absl::StrAppend(&result, absl::StrFormat("0x%x", loc_id));
        continue;
      }

      const auto* location = loc_it->second;

      // Get function name from line→function→name
      if (location->line_size() > 0) {
        const auto& line = location->line(0);
        uint64_t func_id = line.function_id();
        auto func_it = function_map.find(func_id);
        if (func_it != function_map.end()) {
          const auto* function = func_it->second;
          int64_t name_idx = function->name();
          if (name_idx >= 0 && name_idx < profile.string_table_size()) {
            frames.push_back(profile.string_table(name_idx));
          } else {
            frames.push_back(absl::StrFormat("func_%d", func_id));
          }
        } else {
          // Function not found, use address if available
          if (location->address() != 0) {
            frames.push_back(absl::StrFormat("0x%x", location->address()));
          } else {
            frames.push_back(absl::StrFormat("loc_%d", loc_id));
          }
        }
      } else {
        // No line info, use address if available
        if (location->address() != 0) {
          frames.push_back(absl::StrFormat("0x%x", location->address()));
        } else {
          frames.push_back(absl::StrFormat("loc_%d", loc_id));
        }
      }
    }

    if (!frames.empty()) {
      absl::StrAppend(&result, absl::StrJoin(frames, ";"), " ", value, "\n");
    }
  }

  return result;
}

}  // namespace shared
}  // namespace px
