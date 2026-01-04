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

#include <absl/container/flat_hash_map.h>

#include <string>

#include "src/carnot/udf/registry.h"
#include "src/carnot/udf/udf.h"
#include "src/carnot/udfspb/udfs.pb.h"
#include "src/shared/pprof/pprof.h"

namespace px {
namespace carnot {
namespace builtins {

using px::shared::PProfProfile;

class CreatePProfRowAggregate : public udf::UDA {
 public:
  static udf::UDADocBuilder Doc() {
    return udf::UDADocBuilder("Convert perf profiling data to pprof format.")
        .Details("Converts perf profiling stack traces into pprof format.")
        .Example(
            R"doc(
        | # Get the stack traces, the underlying data we want; populate an ASID column
        | # to join with profiler sampling period (see next).
        | stack_traces = px.DataFrame(table='stack_traces.beta', start_time='-1m')
        | stack_traces.asid = px.asid()
        |
        | # Get the profiler sampling period for all deployed PEMs, then merge to stack traces on ASID.
        | sample_period = px.GetProfilerSamplingPeriodMS()
        | df = stack_traces.merge(sample_period, how='inner', left_on=['asid'], right_on=['asid'])
        |
        | # The pprof UDA requires that each underlying dataset have the same sampling period.
        | # Thus, group by sampling period (normally this results in just one group).
        | df = df.groupby(['profiler_sampling_period_ms']).agg(pprof=('stack_trace', 'count', 'profiler_sampling_period_ms', px.pprof))
        )doc")
        .Arg("stack_trace", "Stack trace string.")
        .Arg("count", "Count of the stack trace string.")
        .Arg("profiler_period_ms", "Profiler stack trace sampling period in ms.")
        .Returns("A single row that aggregates all the stack traces and counts into pprof format.");
  }

  void Update(FunctionContext*, const StringValue stack_trace, const Int64Value count,
              const Int64Value profiler_period_ms) {
    UpdateOrCheckSamplingPeriod(profiler_period_ms.val);

    histo_[stack_trace] += count.val;
  }

  void Merge(FunctionContext*, const CreatePProfRowAggregate& other) {
    UpdateOrCheckSamplingPeriod(other.profiler_period_ms_);

    for (const auto& [stack_trace, count] : other.histo_) {
      histo_[stack_trace] += count;
    }
  }

  StringValue Serialize(FunctionContext*) {
    if (multiple_profiler_periods_found_) {
      return "Protobuf `SerializeToString` failed, multiple profiling periods found.";
    }

    const auto pprof = px::shared::CreatePProfProfile(profiler_period_ms_, histo_);
    std::string output;
    const bool ok = pprof.SerializeToString(&output);
    if (!ok) {
      return "Protobuf `SerializeToString` failed.";
    }
    return output;
  }

  Status Deserialize(FunctionContext*, const StringValue& pprof_str) {
    // Parse serialized input a pprof proto object.
    PProfProfile pprof;
    if (!pprof.ParseFromString(pprof_str)) {
      return error::Internal("Could not parse input string into a pprof proto.");
    }

    UpdateOrCheckSamplingPeriod(pprof.period() / 1000 / 1000);

    // Deserialize into a map from stack_trace string to count.
    const auto merge_histo = ::px::shared::DeserializePProfProfile(pprof);

    // Incorporate the deserialized result into our histo_.
    for (const auto& [stack_trace, count] : merge_histo) {
      histo_[stack_trace] += count;
    }
    return Status::OK();
  }

  StringValue Finalize(FunctionContext* ctx) { return Serialize(ctx); }

 protected:
  void UpdateOrCheckSamplingPeriod(const int32_t profiler_period_ms) {
    // Initialize profiler_period_ms_ if needed.
    if (profiler_period_ms_ == -1) {
      profiler_period_ms_ = profiler_period_ms;
    }

    // If any inconsistent profiler period is observed, set the error flag.
    if (profiler_period_ms_ != profiler_period_ms) {
      multiple_profiler_periods_found_ = true;
    }
  }

  absl::flat_hash_map<std::string, uint64_t> histo_;
  int32_t profiler_period_ms_ = -1;
  bool multiple_profiler_periods_found_ = false;
};

/**
 * SymbolizePProf is a UDF that takes a pprof binary and a maps content string
 * (from /proc/<pid>/maps) and returns a symbolized pprof.
 *
 * This allows symbolization of pprof profiles captured from agents by providing
 * the virtual memory mappings at the time of capture.
 */
class SymbolizePProf : public udf::ScalarUDF {
 public:
  /**
   * Symbolize a pprof profile using the provided memory maps.
   *
   * @param pprof_binary The binary-encoded pprof profile (from HeapGrowthStacksUDTF or similar)
   * @param maps_content The content of /proc/<pid>/maps for the process that generated the pprof
   * @return Symbolized pprof profile as a binary-encoded string
   */
  StringValue Exec(FunctionContext*, StringValue pprof_binary, StringValue maps_content);

  // This UDF must run on PEMs because it needs access to the local filesystem
  // to read binaries for symbolization based on the memory mappings.
  static udfspb::UDFSourceExecutor Executor() { return udfspb::UDFSourceExecutor::UDF_PEM; }

  static udf::ScalarUDFDocBuilder Doc() {
    return udf::ScalarUDFDocBuilder("Symbolize a pprof profile using memory mappings.")
        .Details(
            "Takes an unsymbolized pprof profile (binary encoded) and the contents of "
            "/proc/<pid>/maps from the process that generated the profile. Uses the memory "
            "mappings to determine which binary/library each address belongs to, then "
            "symbolizes the addresses using debug information from those binaries.")
        .Example(R"doc(
        | # Get heap growth stacks from agent
        | heap_stacks = px._HeapGrowthStacks()
        | # Get memory maps from the same agent
        | maps = px._DebugAgentProcMaps()
        | # Join on asid and symbolize
        | df = heap_stacks.merge(maps, how='inner', left_on='asid', right_on='asid')
        | df.symbolized = px.symbolize_pprof(df.heap, df.maps_content)
        )doc")
        .Arg("pprof_binary", "Binary-encoded pprof profile to symbolize")
        .Arg("maps_content", "Contents of /proc/<pid>/maps for the source process")
        .Returns("Symbolized pprof profile as binary-encoded string");
  }
};

/**
 * PProfToFoldedStacksUDF converts pprof data to folded stack trace format.
 *
 * Takes pprof data (binary protobuf, typically already symbolized via SymbolizePProf)
 * and returns folded stacks suitable for flame graph generation
 * (e.g., Brendan Gregg's FlameGraph tools).
 *
 * Output format: "root;caller1;caller2;leaf value\n" per unique stack.
 */
class PProfToFoldedStacksUDF : public udf::ScalarUDF {
 public:
  /**
   * Convert pprof data to folded stacks format.
   *
   * @param pprof_data The pprof data (binary protobuf, should be symbolized first)
   * @param value_index Which sample value to use (0=alloc_objects, 1=alloc_space)
   * @return Folded stack trace string
   */
  StringValue Exec(FunctionContext*, StringValue pprof_data, Int64Value value_index);

  static udf::ScalarUDFDocBuilder Doc() {
    return udf::ScalarUDFDocBuilder("Convert pprof data to folded stack trace format.")
        .Details(
            "Takes pprof binary protobuf data (should be symbolized first via symbolize_pprof) "
            "and returns a folded stack trace format suitable for flame graph tools.\n\n"
            "Output format: 'root;caller1;caller2;leaf value' per line, where value is either "
            "allocation count (value_index=0) or bytes allocated (value_index=1).")
        .Example(R"doc(
        | # Get heap growth stacks and maps from agent
        | heap = px._HeapGrowthStacks()
        | maps = px._DebugAgentProcMaps()
        | # Join, symbolize, then convert to folded format
        | df = heap.merge(maps, how='inner', left_on='asid', right_on='asid')
        | df.symbolized = px.symbolize_pprof(df.pprof_data, df.maps_content)
        | df.folded = px.pprof_to_folded_stacks(df.symbolized, 1)
        )doc")
        .Arg("pprof_data", "PProf binary protobuf data (should be symbolized first)")
        .Arg("value_index",
             "Which sample value to use: 0=alloc_objects (count), 1=alloc_space (bytes)")
        .Returns("Folded stack trace string for flame graph generation");
  }
};

void RegisterPProfOpsOrDie(udf::Registry* registry);

}  // namespace builtins
}  // namespace carnot
}  // namespace px
