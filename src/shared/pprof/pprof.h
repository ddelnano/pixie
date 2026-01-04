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

#pragma once

#include <string>

#include <absl/container/flat_hash_map.h>
#include "src/common/base/statusor.h"

#include "proto/profile.pb.h"

namespace px {
namespace shared {

using PProfProfile = ::perftools::profiles::Profile;
using PProfHisto = absl::flat_hash_map<std::string, uint64_t>;

// https://github.com/google/pprof/blob/main/proto/profile.proto
PProfProfile CreatePProfProfile(const uint32_t period_ms, const PProfHisto& histo);
PProfHisto DeserializePProfProfile(const PProfProfile& pprof);

// Parse gperftools legacy text format (from GetHeapGrowthStacks/GetHeapSample) into pprof protobuf.
// The legacy format looks like:
//   heap profile:    170: 430301184 [   170: 430301184] @ growth
//        1: 23642112 [     1: 23642112] @ 0x57ab4121650a 0x57ab412846f3 ...
//
// This creates a pprof profile with:
// - Locations containing addresses (unsymbolized)
// - Sample values for allocation count and bytes
// - No mappings (those should be added separately from /proc/pid/maps)
StatusOr<PProfProfile> ParseLegacyHeapProfile(const std::string& legacy_text);

// Represents a memory mapping entry from /proc/<pid>/maps
struct MemoryMapping {
  uint64_t vmem_start;
  uint64_t vmem_end;
  std::string permissions;
  uint64_t file_offset;
  std::string pathname;

  // Check if an address falls within this mapping
  bool Contains(uint64_t addr) const { return addr >= vmem_start && addr < vmem_end; }

  // Convert a virtual address to binary (file) address
  uint64_t VirtualToBinaryAddr(uint64_t virtual_addr) const {
    return virtual_addr - vmem_start + file_offset;
  }
};

// Parse /proc/<pid>/maps format into a vector of MemoryMapping entries
StatusOr<std::vector<MemoryMapping>> ParseProcMaps(const std::string& maps_content);

// Symbolize a pprof profile using memory mappings and ELF symbol tables.
// This adds Function entries with resolved symbol names for each Location.
//
// @param profile The pprof profile to symbolize (modified in place)
// @param mappings Memory mappings from /proc/<pid>/maps
// @return Status indicating success or failure
Status SymbolizePProfProfile(PProfProfile* profile, const std::vector<MemoryMapping>& mappings);

// Convert a pprof profile to folded stack trace format.
// The folded format is used by Brendan Gregg's FlameGraph tools:
//   root;caller1;caller2;leaf value
//   root;caller1;other_leaf value
//
// Each line represents a unique stack with its sample value.
// Stack order is root→leaf (reversed from pprof's leaf→root storage).
//
// @param profile The pprof profile (should be symbolized for meaningful output)
// @param value_index Which sample value to use (0 = alloc_objects/count, 1 = alloc_space/bytes)
// @return Folded stack trace string, or error status
StatusOr<std::string> PProfToFoldedStacks(const PProfProfile& profile, int value_index = 1);

}  // namespace shared
}  // namespace px
