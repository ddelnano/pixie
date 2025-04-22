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

#include "src/carnot/exec/filter_node.h"

#include <arrow/array.h>
#include <arrow/api.h>
#include <arrow/array/builder_binary.h>
#include <arrow/array/concatenate.h>
#include <arrow/memory_pool.h>
#include <arrow/status.h>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include <absl/strings/substitute.h>

#include "src/carnot/plan/scalar_expression.h"
#include "src/carnot/planpb/plan.pb.h"
#include "src/carnot/udf/udf_wrapper.h"
#include "src/common/base/base.h"
#include "src/shared/types/arrow_adapter.h"
#include "src/shared/types/column_wrapper.h"
#include "src/shared/types/type_utils.h"
#include "src/shared/types/types.h"
#include "src/shared/types/typespb/wrapper/types_pb_wrapper.h"

namespace px {
namespace carnot {
namespace exec {

using table_store::schema::RowBatch;
using table_store::schema::RowDescriptor;

std::string FilterNode::DebugStringImpl() {
  return absl::Substitute("Exec::FilterNode<$0>", plan_node_->DebugString());
}

Status FilterNode::InitImpl(const plan::Operator& plan_node) {
  CHECK(plan_node.op_type() == planpb::OperatorType::FILTER_OPERATOR);
  const auto* filter_plan_node = static_cast<const plan::FilterOperator*>(&plan_node);
  // copy the plan node to local object;
  plan_node_ = std::make_unique<plan::FilterOperator>(*filter_plan_node);
  return Status::OK();
}

Status FilterNode::PrepareImpl(ExecState* exec_state) {
  function_ctx_ = exec_state->CreateFunctionContext();
  evaluator_ = std::make_unique<VectorNativeScalarExpressionEvaluator>(
      plan::ConstScalarExpressionVector{plan_node_->expression()}, function_ctx_.get());
  return Status::OK();
}

Status FilterNode::OpenImpl(ExecState* exec_state) {
  PX_RETURN_IF_ERROR(evaluator_->Open(exec_state));
  return Status::OK();
}

Status FilterNode::CloseImpl(ExecState* exec_state) {
  PX_RETURN_IF_ERROR(evaluator_->Close(exec_state));
  return Status::OK();
}

template <types::DataType T>
Status PredicateCopyValues(const types::BoolValueColumnWrapper& pred, std::shared_ptr<arrow::Array> input_col,
                           RowBatch* output_rb) {
  DCHECK_EQ(pred.Size(), static_cast<size_t>(input_col->length()));
  std::vector<std::shared_ptr<arrow::Array>> chunks;
  size_t num_input_records = input_col->length();
  int64_t segment_start = -1;
  for (size_t idx = 0; idx < num_input_records; ++idx) {
    bool passed_pred = udf::UnWrap(pred[idx]);
    if (passed_pred) {
      if (segment_start == -1) {
        segment_start = idx;
      }
    } else {
      if (segment_start != -1) {
        auto sliced_arr = input_col->Slice(segment_start, idx - segment_start);
        chunks.push_back(sliced_arr);
        segment_start = -1;
      }
    }
  }
  auto mem_pool = arrow::default_memory_pool();
  auto logging_mem_pool = std::make_unique<arrow::LoggingMemoryPool>(mem_pool);
  PX_UNUSED(logging_mem_pool);
  auto starting_bytes = mem_pool->bytes_allocated();
  if (chunks.empty() && segment_start != -1) {
    // We have a single segment that doesn't transition to false.
    auto sliced_arr = input_col->Slice(segment_start, num_input_records - segment_start);
    chunks.push_back(sliced_arr);
  } else if (chunks.empty()) {
    std::shared_ptr<arrow::Array> empty_arr;
    auto output_col_builder_generic = MakeArrowBuilder(T, mem_pool);
    PX_RETURN_IF_ERROR(output_col_builder_generic->Reserve(output_rb->num_rows()));
    PX_RETURN_IF_ERROR(output_col_builder_generic->Finish(&empty_arr));
    PX_RETURN_IF_ERROR(output_rb->AddColumn(empty_arr));
    LOG(INFO) << "Allocated " << mem_pool->bytes_allocated() - starting_bytes << " bytes";
    return Status::OK();
  }
  std::shared_ptr<arrow::ChunkedArray> combined_arr;
  auto status = arrow::Concatenate(chunks, mem_pool, &combined_arr);
  LOG(INFO) << "Allocated " << mem_pool->bytes_allocated() - starting_bytes << " bytes";
  if (!status.ok()) {
    return error::Internal("Failed to concatenate arrays: $0", status.message());
  }
  PX_RETURN_IF_ERROR(output_rb->AddColumn(combined_arr));
  return Status::OK();
}

Status FilterNode::ConsumeNextImpl(ExecState* exec_state, const RowBatch& rb, size_t) {
  // Current implementation does not merge across row batches, we should
  // consider this for cases where the filter has really low selectivity.
  PX_ASSIGN_OR_RETURN(auto pred_col, evaluator_->EvaluateSingleExpression(
                                         exec_state, rb, *plan_node_->expression()));

  // Verify that the type of the column is boolean.
  DCHECK_EQ(pred_col->data_type(), types::BOOLEAN) << "Predicate expression must be a boolean";

  const types::BoolValueColumnWrapper& pred_col_wrapper =
      *static_cast<types::BoolValueColumnWrapper*>(pred_col.get());
  size_t num_pred = pred_col_wrapper.Size();

  DCHECK_EQ(static_cast<size_t>(rb.num_rows()), num_pred);

  // Find out how many of them returned true;
  size_t num_output_records = 0;
  for (size_t i = 0; i < num_pred; ++i) {
    if (pred_col_wrapper[i].val) {
      ++num_output_records;
    }
  }

  RowBatch output_rb(*output_descriptor_, num_output_records);
  DCHECK_EQ(output_descriptor_->size(), plan_node_->selected_cols().size());

  for (const auto& [output_col_idx, input_col_idx] : Enumerate(plan_node_->selected_cols())) {
    auto input_col = rb.ColumnAt(input_col_idx);
    auto col_type = output_descriptor_->type(output_col_idx);
#define TYPE_CASE(_dt_) \
  PX_RETURN_IF_ERROR(PredicateCopyValues<_dt_>(pred_col_wrapper, input_col, &output_rb));
    PX_SWITCH_FOREACH_DATATYPE(col_type, TYPE_CASE);
#undef TYPE_CASE
  }

  output_rb.set_eow(rb.eow());
  output_rb.set_eos(rb.eos());
  PX_RETURN_IF_ERROR(SendRowBatchToChildren(exec_state, output_rb));
  return Status::OK();
}

}  // namespace exec
}  // namespace carnot
}  // namespace px
