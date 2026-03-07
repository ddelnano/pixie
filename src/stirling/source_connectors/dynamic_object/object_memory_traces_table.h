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

#include "src/stirling/core/canonical_types.h"
#include "src/stirling/core/output.h"
#include "src/stirling/core/types.h"

namespace px {
namespace stirling {

// clang-format off
constexpr DataElement kObjectMemoryTracesElements[] = {
    canonical_data_elements::kTime,
    canonical_data_elements::kUPID,
    {"stack_trace",
     "Semicolon-delimited path from object root to member, in folded format.",
     types::DataType::STRING, types::SemanticType::ST_NONE, types::PatternType::GENERAL},
    {"count",
     "Memory size in bytes (staticSize + dynamicSize) at this node.",
     types::DataType::INT64, types::SemanticType::ST_NONE, types::PatternType::METRIC_GAUGE},
    {"padding_savings",
     "Potential padding savings in bytes at this node.",
     types::DataType::INT64, types::SemanticType::ST_NONE, types::PatternType::METRIC_GAUGE},
};

constexpr auto kObjectMemoryTracesTable = DataTableSchema(
    "object_memory_traces",
    "Object memory hierarchy traces for flamegraph visualization.",
    kObjectMemoryTracesElements);
// clang-format on
DEFINE_PRINT_TABLE(ObjectMemoryTraces)

constexpr int kObjectMemoryTracesTimeIdx = kObjectMemoryTracesTable.ColIndex("time_");
constexpr int kObjectMemoryTracesUPIDIdx = kObjectMemoryTracesTable.ColIndex("upid");
constexpr int kObjectMemoryTracesStackTraceIdx = kObjectMemoryTracesTable.ColIndex("stack_trace");
constexpr int kObjectMemoryTracesCountIdx = kObjectMemoryTracesTable.ColIndex("count");
constexpr int kObjectMemoryTracesPaddingSavingsIdx =
    kObjectMemoryTracesTable.ColIndex("padding_savings");

}  // namespace stirling
}  // namespace px
