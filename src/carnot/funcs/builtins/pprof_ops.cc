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

#include "src/carnot/funcs/builtins/pprof_ops.h"

#include <absl/strings/str_cat.h>

#include "src/carnot/udf/registry.h"
#include "src/common/base/base.h"

namespace px {
namespace carnot {
namespace builtins {

using px::shared::PProfProfile;

types::StringValue SymbolizePProf::Exec(FunctionContext*, types::StringValue pprof_data,
                                        types::StringValue maps_content) {
  // Validate maps_content is not empty
  if (maps_content.empty()) {
    return "Error: Empty maps content";
  }

  PProfProfile pprof;

  // Try to parse as binary protobuf first
  bool parsed_as_binary = pprof.ParseFromString(pprof_data);

  if (!parsed_as_binary) {
    // Try to parse as legacy gperftools text format
    auto legacy_result = px::shared::ParseLegacyHeapProfile(pprof_data);
    if (!legacy_result.ok()) {
      return absl::StrCat("Error: Failed to parse pprof data: ", legacy_result.status().msg());
    }
    pprof = legacy_result.ConsumeValueOrDie();
  }

  // Parse the memory mappings
  auto mappings_result = px::shared::ParseProcMaps(maps_content);
  if (!mappings_result.ok()) {
    return absl::StrCat("Error: Failed to parse maps: ", mappings_result.status().msg());
  }
  auto mappings = mappings_result.ConsumeValueOrDie();

  // Symbolize the pprof profile
  auto symbolize_status = px::shared::SymbolizePProfProfile(&pprof, mappings);
  if (!symbolize_status.ok()) {
    return absl::StrCat("Error: Failed to symbolize: ", symbolize_status.msg());
  }

  // Serialize back to binary protobuf format
  std::string output;
  if (!pprof.SerializeToString(&output)) {
    return "Error: Failed to serialize pprof to binary";
  }

  return output;
}

types::StringValue PProfToFoldedStacksUDF::Exec(FunctionContext*, types::StringValue pprof_data,
                                                 types::Int64Value value_index) {
  PProfProfile pprof;

  // Parse as binary protobuf
  if (!pprof.ParseFromString(pprof_data)) {
    return "Error: Failed to parse pprof data as binary protobuf";
  }

  // Convert to folded stacks format
  auto folded_result = px::shared::PProfToFoldedStacks(pprof, value_index.val);
  if (!folded_result.ok()) {
    return absl::StrCat("Error: Failed to convert to folded stacks: ",
                        folded_result.status().msg());
  }

  return folded_result.ConsumeValueOrDie();
}

void RegisterPProfOpsOrDie(udf::Registry* registry) {
  CHECK(registry != nullptr);

  registry->RegisterOrDie<CreatePProfRowAggregate>("pprof");
  registry->RegisterOrDie<SymbolizePProf>("symbolize_pprof");
  registry->RegisterOrDie<PProfToFoldedStacksUDF>("pprof_to_folded_stacks");
}

}  // namespace builtins
}  // namespace carnot
}  // namespace px
