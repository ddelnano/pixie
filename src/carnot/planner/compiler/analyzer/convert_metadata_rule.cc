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

#include <memory>
#include <vector>

#include "src/carnot/planner/compiler/analyzer/convert_metadata_rule.h"
#include "src/carnot/planner/ir/column_ir.h"
#include "src/carnot/planner/ir/filter_ir.h"
#include "src/carnot/planner/ir/func_ir.h"
#include "src/carnot/planner/ir/map_ir.h"
#include "src/carnot/planner/ir/metadata_ir.h"

namespace px {
namespace carnot {
namespace planner {
namespace compiler {

Status ConvertMetadataRule::UpdateMetadataContainer(IRNode* container, MetadataIR* metadata,
                                                    ExpressionIR* metadata_expr) const {
  if (Match(container, Func())) {
    auto func = static_cast<FuncIR*>(container);
    PX_RETURN_IF_ERROR(func->UpdateArg(metadata, metadata_expr));
    return Status::OK();
  }
  if (Match(container, Map())) {
    auto map = static_cast<MapIR*>(container);
    for (const auto& expr : map->col_exprs()) {
      if (expr.node == metadata) {
        PX_RETURN_IF_ERROR(map->UpdateColExpr(expr.name, metadata_expr));
      }
    }
    return Status::OK();
  }
  if (Match(container, Filter())) {
    auto filter = static_cast<FilterIR*>(container);
    return filter->SetFilterExpr(metadata_expr);
  }
  return error::Internal("Unsupported IRNode container for metadata: $0", container->DebugString());
}

StatusOr<std::string> ConvertMetadataRule::FindKeyColumn(std::shared_ptr<TableType> parent_type,
                                                         MetadataProperty* property,
                                                         IRNode* node_for_error) const {
  DCHECK_NE(property, nullptr);
  for (const std::string& key_col : property->GetKeyColumnReprs()) {
    if (parent_type->HasColumn(key_col)) {
      return key_col;
    }
  }
  return node_for_error->CreateIRNodeError(
      "Can't resolve metadata because of lack of converting columns in the parent. Need one of "
      "[$0]. Parent type has columns [$1] available.",
      absl::StrJoin(property->GetKeyColumnReprs(), ","),
      absl::StrJoin(parent_type->ColumnNames(), ","));
}

bool CheckBackupConversionAvailable(std::shared_ptr<TableType> parent_type,
                                                         const std::string& func_name) {
  return parent_type->HasColumn("time_") && parent_type->HasColumn("local_addr") && func_name == "upid_to_pod_name";
}

StatusOr<bool> ConvertMetadataRule::Apply(IRNode* ir_node) {
  if (!Match(ir_node, Metadata())) {
    return false;
  }

  auto graph = ir_node->graph();
  auto metadata = static_cast<MetadataIR*>(ir_node);
  auto md_property = metadata->property();
  auto parent_op_idx = metadata->container_op_parent_idx();
  auto column_type = md_property->column_type();
  auto md_type = md_property->metadata_type();

  PX_ASSIGN_OR_RETURN(auto parent, metadata->ReferencedOperator());
  PX_ASSIGN_OR_RETURN(auto containing_ops, metadata->ContainingOperators());

  PX_ASSIGN_OR_RETURN(std::string key_column_name,
                      FindKeyColumn(parent->resolved_table_type(), md_property, ir_node));

  PX_ASSIGN_OR_RETURN(ColumnIR * key_column,
                      graph->CreateNode<ColumnIR>(ir_node->ast(), key_column_name, parent_op_idx));

  PX_ASSIGN_OR_RETURN(std::string func_name, md_property->UDFName(key_column_name));
  PX_ASSIGN_OR_RETURN(
      FuncIR * conversion_func,
      graph->CreateNode<FuncIR>(ir_node->ast(), FuncIR::Op{FuncIR::Opcode::non_op, "", func_name},
                                std::vector<ExpressionIR*>{key_column}));

  // TODO(ddelnano): Until the short lived process issue (gh#1638) is resolved, use a backup UDF via local_addr
  // in case the upid_to_pod_name fails to resolve the pod name
  auto backup_conversion_available = CheckBackupConversionAvailable(parent->resolved_table_type(), func_name);
  if (backup_conversion_available) {
    PX_ASSIGN_OR_RETURN(ColumnIR * local_addr_col,
                        graph->CreateNode<ColumnIR>(ir_node->ast(), "local_addr", parent_op_idx));
    PX_ASSIGN_OR_RETURN(ColumnIR * time_col,
                        graph->CreateNode<ColumnIR>(ir_node->ast(), "time_", parent_op_idx));
    PX_ASSIGN_OR_RETURN(
        FuncIR *ip_conversion_func,
        graph->CreateNode<FuncIR>(ir_node->ast(), FuncIR::Op{FuncIR::Opcode::non_op, "", "_ip_to_pod_id_pem_exec"},
                                  std::vector<ExpressionIR*>{local_addr_col, time_col}));
    PX_ASSIGN_OR_RETURN(
        FuncIR *backup_conversion_func,
        graph->CreateNode<FuncIR>(ir_node->ast(), FuncIR::Op{FuncIR::Opcode::non_op, "", "pod_id_to_pod_name"},
                              std::vector<ExpressionIR*>{static_cast<ExpressionIR*>(ip_conversion_func)}));
    auto empty_string = static_cast<ExpressionIR*>(graph->CreateNode<StringIR>(ir_node->ast(), "").ConsumeValueOrDie());
    PX_ASSIGN_OR_RETURN(
        FuncIR *select_expr,
        graph->CreateNode<FuncIR>(ir_node->ast(), FuncIR::Op{FuncIR::Opcode::eq, "==", "equal"},
                                  std::vector<ExpressionIR*>{static_cast<ExpressionIR*>(conversion_func), empty_string}));
    PX_ASSIGN_OR_RETURN(
        auto second_func,
        graph->CopyNode<FuncIR>(conversion_func));
    PX_ASSIGN_OR_RETURN(
        FuncIR *select_func,
        graph->CreateNode<FuncIR>(ir_node->ast(), FuncIR::Op{FuncIR::Opcode::non_op, "", "select"},
                                  std::vector<ExpressionIR*>{static_cast<ExpressionIR*>(select_expr), backup_conversion_func, second_func}));

    conversion_func = select_func;
  }

  for (int64_t parent_id : graph->dag().ParentsOf(metadata->id())) {
    // For each container node of the metadata expression, update it to point to the
    // new conversion func instead.
    PX_RETURN_IF_ERROR(UpdateMetadataContainer(graph->Get(parent_id), metadata, conversion_func));
  }

  // Propagate type changes from the new conversion_func.
  PX_RETURN_IF_ERROR(PropagateTypeChangesFromNode(graph, conversion_func, compiler_state_));

  DCHECK_EQ(conversion_func->EvaluatedDataType(), column_type)
      << "Expected the parent key column type and metadata property type to match.";
  conversion_func->set_annotations(ExpressionIR::Annotations(md_type));
  return true;
}

}  // namespace compiler
}  // namespace planner
}  // namespace carnot
}  // namespace px
