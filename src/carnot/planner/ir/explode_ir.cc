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

#include "src/carnot/planner/ir/explode_ir.h"
#include "src/carnot/planner/ir/ir.h"

namespace px {
namespace carnot {
namespace planner {

Status ExplodeIR::Init(OperatorIR* parent, ColumnIR* explode_column,
                       const std::string& delimiter) {
  PX_RETURN_IF_ERROR(AddParent(parent));
  PX_RETURN_IF_ERROR(SetExplodeColumn(explode_column));
  return SetDelimiter(delimiter);
}

std::string ExplodeIR::DebugString() const {
  return absl::Substitute("$0(id=$1, column=$2, delimiter='$3')", type_string(), id(),
                          explode_column_ ? explode_column_->DebugString() : "null", delimiter_);
}

Status ExplodeIR::SetExplodeColumn(ColumnIR* column) {
  auto old_column = explode_column_;
  if (old_column) {
    PX_RETURN_IF_ERROR(graph()->DeleteEdge(this, old_column));
  }
  PX_ASSIGN_OR_RETURN(explode_column_, graph()->OptionallyCloneWithEdge(this, column));
  if (old_column) {
    PX_RETURN_IF_ERROR(graph()->DeleteOrphansInSubtree(old_column->id()));
  }
  return Status::OK();
}

Status ExplodeIR::SetDelimiter(const std::string& delimiter) {
  delimiter_ = delimiter;
  return Status::OK();
}

StatusOr<std::vector<absl::flat_hash_set<std::string>>> ExplodeIR::RequiredInputColumns() const {
  DCHECK(is_type_resolved());
  absl::flat_hash_set<std::string> required_cols;
  // Need the explode column.
  required_cols.insert(explode_column_->col_name());
  // Also need all columns that are in the output.
  required_cols.insert(resolved_table_type()->ColumnNames().begin(),
                       resolved_table_type()->ColumnNames().end());
  return std::vector<absl::flat_hash_set<std::string>>{required_cols};
}

StatusOr<absl::flat_hash_set<std::string>> ExplodeIR::PruneOutputColumnsToImpl(
    const absl::flat_hash_set<std::string>& output_cols) {
  // Make sure we keep the explode column in the output.
  absl::flat_hash_set<std::string> result = output_cols;
  result.insert(explode_column_->col_name());
  return result;
}

Status ExplodeIR::ToProto(planpb::Operator* op) const {
  auto pb = op->mutable_explode_op();
  op->set_op_type(planpb::EXPLODE_OPERATOR);
  DCHECK_EQ(parents().size(), 1UL);

  auto parent_table_type = parents()[0]->resolved_table_type();
  auto explode_col_name = explode_column_->col_name();

  // Find the index of the explode column in the parent.
  DCHECK(parent_table_type->HasColumn(explode_col_name))
      << absl::Substitute("Don't have $0 in parent_relation $1", explode_col_name,
                          parent_table_type->DebugString());
  pb->set_explode_column_index(parent_table_type->GetColumnIndex(explode_col_name));
  pb->set_delimiter(delimiter_);

  auto col_names = resolved_table_type()->ColumnNames();
  for (const auto& col_name : col_names) {
    planpb::Column* col_pb = pb->add_columns();
    col_pb->set_node(parents()[0]->id());
    DCHECK(parent_table_type->HasColumn(col_name)) << absl::Substitute(
        "Don't have $0 in parent_relation $1", col_name, parent_table_type->DebugString());
    col_pb->set_index(parent_table_type->GetColumnIndex(col_name));
    pb->add_column_names(col_name);
  }

  return Status::OK();
}

Status ExplodeIR::CopyFromNodeImpl(const IRNode* node,
                                   absl::flat_hash_map<const IRNode*, IRNode*>* copied_nodes_map) {
  const ExplodeIR* explode = static_cast<const ExplodeIR*>(node);
  PX_ASSIGN_OR_RETURN(ColumnIR * new_column,
                      graph()->CopyNode(explode->explode_column_, copied_nodes_map));
  PX_RETURN_IF_ERROR(SetExplodeColumn(new_column));
  delimiter_ = explode->delimiter_;
  return Status::OK();
}

Status ExplodeIR::ResolveType(CompilerState* compiler_state) {
  PX_RETURN_IF_ERROR(ResolveExpressionType(explode_column_, compiler_state, parent_types()));

  // Validate that the explode column is of STRING type.
  if (explode_column_->EvaluatedDataType() != types::STRING) {
    return CreateIRNodeError("Explode column must be of STRING type, got $0",
                             types::ToString(explode_column_->EvaluatedDataType()));
  }

  // The output type is the same as the input type (all columns pass through).
  PX_ASSIGN_OR_RETURN(auto type_ptr, OperatorIR::DefaultResolveType(parent_types()));
  return SetResolvedType(type_ptr);
}

}  // namespace planner
}  // namespace carnot
}  // namespace px
