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
constexpr DataElement kObjectContainerStatsElements[] = {
    canonical_data_elements::kTime,
    canonical_data_elements::kUPID,
    {"container_path",
     "Dot-delimited path to the container within the object.",
     types::DataType::STRING, types::SemanticType::ST_NONE, types::PatternType::GENERAL},
    {"container_type",
     "C++ type of the container.",
     types::DataType::STRING, types::SemanticType::ST_NONE, types::PatternType::GENERAL},
    {"element_count",
     "Number of elements in the container at capture time.",
     types::DataType::INT64, types::SemanticType::ST_NONE, types::PatternType::METRIC_GAUGE},
    {"total_size",
     "Total memory (static + dynamic) consumed by this container.",
     types::DataType::INT64, types::SemanticType::ST_NONE, types::PatternType::METRIC_GAUGE},
};

constexpr auto kObjectContainerStatsTable = DataTableSchema(
    "object_container_stats",
    "Container element count statistics for histogram visualization.",
    kObjectContainerStatsElements);
// clang-format on
DEFINE_PRINT_TABLE(ObjectContainerStats)

constexpr int kObjectContainerStatsTimeIdx = kObjectContainerStatsTable.ColIndex("time_");
constexpr int kObjectContainerStatsUPIDIdx = kObjectContainerStatsTable.ColIndex("upid");
constexpr int kObjectContainerStatsContainerPathIdx =
    kObjectContainerStatsTable.ColIndex("container_path");
constexpr int kObjectContainerStatsContainerTypeIdx =
    kObjectContainerStatsTable.ColIndex("container_type");
constexpr int kObjectContainerStatsElementCountIdx =
    kObjectContainerStatsTable.ColIndex("element_count");
constexpr int kObjectContainerStatsTotalSizeIdx =
    kObjectContainerStatsTable.ColIndex("total_size");

}  // namespace stirling
}  // namespace px
