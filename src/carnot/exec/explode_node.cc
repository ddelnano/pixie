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

#include "src/carnot/exec/explode_node.h"

#include <arrow/array.h>
#include <arrow/array/builder_binary.h>
#include <arrow/memory_pool.h>
#include <arrow/status.h>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include <absl/strings/str_split.h>
#include <absl/strings/substitute.h>

#include "src/carnot/planpb/plan.pb.h"
#include "src/common/base/base.h"
#include "src/shared/types/arrow_adapter.h"
#include "src/shared/types/column_wrapper.h"
#include "src/shared/types/type_utils.h"
#include "src/shared/types/types.h"

namespace px {
namespace carnot {
namespace exec {

using table_store::schema::RowBatch;
using table_store::schema::RowDescriptor;

std::string ExplodeNode::DebugStringImpl() {
  return absl::Substitute("Exec::ExplodeNode<$0>", plan_node_->DebugString());
}

Status ExplodeNode::InitImpl(const plan::Operator& plan_node) {
  CHECK(plan_node.op_type() == planpb::OperatorType::EXPLODE_OPERATOR);
  const auto* explode_plan_node = static_cast<const plan::ExplodeOperator*>(&plan_node);
  plan_node_ = std::make_unique<plan::ExplodeOperator>(*explode_plan_node);
  return Status::OK();
}

Status ExplodeNode::PrepareImpl(ExecState*) { return Status::OK(); }

Status ExplodeNode::OpenImpl(ExecState*) { return Status::OK(); }

Status ExplodeNode::CloseImpl(ExecState*) { return Status::OK(); }

namespace {

// Helper to copy a value from input to output for non-string types.
template <types::DataType T>
void AppendValue(const arrow::Array* input_col, size_t input_idx,
                 typename types::DataTypeTraits<T>::arrow_builder_type* builder) {
  builder->UnsafeAppend(types::GetValueFromArrowArray<T>(input_col, input_idx));
}

// Specialization for string type - we need the explicit string value to append.
template <>
void AppendValue<types::STRING>(const arrow::Array* input_col, size_t input_idx,
                                arrow::StringBuilder* builder) {
  auto val = types::GetValueFromArrowArray<types::STRING>(input_col, input_idx);
  // For strings, we can't use UnsafeAppend without proper reservation.
  auto status = builder->Append(val);
  DCHECK(status.ok());
}

}  // namespace

Status ExplodeNode::ConsumeNextImpl(ExecState* exec_state, const RowBatch& rb, size_t) {
  const auto& selected_cols = plan_node_->selected_cols();
  int64_t explode_col_idx = plan_node_->explode_column_index();
  const std::string& delimiter = plan_node_->delimiter();

  // Find the position of the explode column in the selected columns.
  int64_t explode_output_idx = -1;
  for (size_t i = 0; i < selected_cols.size(); ++i) {
    if (selected_cols[i] == explode_col_idx) {
      explode_output_idx = static_cast<int64_t>(i);
      break;
    }
  }
  DCHECK_GE(explode_output_idx, 0) << "Explode column must be in selected columns";

  // Get the explode column from the input.
  auto explode_col = rb.ColumnAt(explode_col_idx);
  DCHECK_EQ(explode_col->type_id(), arrow::Type::STRING)
      << "Explode column must be a string type";

  // First pass: count total output rows by splitting each explode value.
  // We store the original string values to keep them alive, then split into string_views.
  std::vector<std::string> original_values(rb.num_rows());
  std::vector<std::vector<std::string>> split_values(rb.num_rows());
  size_t total_output_rows = 0;
  for (int64_t row = 0; row < rb.num_rows(); ++row) {
    original_values[row] = types::GetValueFromArrowArray<types::STRING>(explode_col.get(), row);
    // Split and convert to strings.
    for (const auto& sv : absl::StrSplit(original_values[row], delimiter)) {
      split_values[row].emplace_back(sv);
    }
    // If the value is empty or splits to nothing, we still produce at least one row.
    if (split_values[row].empty()) {
      split_values[row].emplace_back("");
    }
    total_output_rows += split_values[row].size();
  }

  // Create output row batch.
  RowBatch output_rb(*output_descriptor_, total_output_rows);

  // For each output column, build the expanded array.
  for (size_t out_col_idx = 0; out_col_idx < selected_cols.size(); ++out_col_idx) {
    auto input_col_idx = selected_cols[out_col_idx];
    auto input_col = rb.ColumnAt(input_col_idx);
    auto col_type = output_descriptor_->type(out_col_idx);

    if (static_cast<int64_t>(out_col_idx) == explode_output_idx) {
      // This is the explode column - output the split values.
      auto builder_generic = MakeArrowBuilder(types::STRING, arrow::default_memory_pool());
      auto* builder = static_cast<arrow::StringBuilder*>(builder_generic.get());

      for (int64_t row = 0; row < rb.num_rows(); ++row) {
        for (const auto& split_val : split_values[row]) {
          PX_RETURN_IF_ERROR(builder->Append(split_val));
        }
      }

      std::shared_ptr<arrow::Array> output_array;
      PX_RETURN_IF_ERROR(builder->Finish(&output_array));
      PX_RETURN_IF_ERROR(output_rb.AddColumn(output_array));
    } else {
      // Non-explode column - replicate values for each split.
#define TYPE_CASE(_dt_)                                                                      \
  {                                                                                          \
    auto builder_generic = MakeArrowBuilder(_dt_, arrow::default_memory_pool());             \
    auto* builder = static_cast<typename types::DataTypeTraits<_dt_>::arrow_builder_type*>(  \
        builder_generic.get());                                                              \
    PX_RETURN_IF_ERROR(builder->Reserve(total_output_rows));                                 \
    for (int64_t row = 0; row < rb.num_rows(); ++row) {                                      \
      size_t replicate_count = split_values[row].size();                                     \
      for (size_t rep = 0; rep < replicate_count; ++rep) {                                   \
        AppendValue<_dt_>(input_col.get(), row, builder);                                    \
      }                                                                                      \
    }                                                                                        \
    std::shared_ptr<arrow::Array> output_array;                                              \
    PX_RETURN_IF_ERROR(builder->Finish(&output_array));                                      \
    PX_RETURN_IF_ERROR(output_rb.AddColumn(output_array));                                   \
  }
      PX_SWITCH_FOREACH_DATATYPE(col_type, TYPE_CASE);
#undef TYPE_CASE
    }
  }

  output_rb.set_eow(rb.eow());
  output_rb.set_eos(rb.eos());
  PX_RETURN_IF_ERROR(SendRowBatchToChildren(exec_state, output_rb));
  return Status::OK();
}

}  // namespace exec
}  // namespace carnot
}  // namespace px
