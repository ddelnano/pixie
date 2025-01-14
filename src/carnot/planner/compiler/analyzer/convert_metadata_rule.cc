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

namespace {
std::string GetUniquePodNameCol(std::shared_ptr<TableType> parent_type,
                                absl::flat_hash_set<std::string>* used_column_names) {
  auto col_name_counter = 0;
  do {
    auto new_col = absl::StrCat("pod_name_", col_name_counter++);
    if (!used_column_names->contains(new_col) && !parent_type->HasColumn(new_col)) {
      used_column_names->insert(new_col);
      return new_col;
    }
  } while (true);
}
}  // namespace

Status ConvertMetadataRule::AddOptimisticPodNameConversionMap(
    IR* graph, IRNode* container, ExpressionIR* metadata_expr, ExpressionIR* fallback_expr,
    const std::pair<std::string, std::string>& col_names) const {
  LOG(INFO) << "Adding optimistic pod name conversion map";
  if (Match(container, Func())) {
    for (int64_t parent_id : graph->dag().ParentsOf(container->id())) {
      PX_RETURN_IF_ERROR(AddOptimisticPodNameConversionMap(
          graph, graph->Get(parent_id), metadata_expr, fallback_expr, col_names));
    }
  } else if (Match(container, Operator())) {
    for (int64_t parent_id : graph->dag().ParentsOf(container->id())) {
      auto parent = graph->Get(parent_id);
      if (!Match(parent, Operator())) {
        continue;
      }
      auto parent_op = static_cast<OperatorIR*>(parent);
      auto container_op = static_cast<OperatorIR*>(container);

      PX_ASSIGN_OR_RETURN(auto map_ir,
                          graph->CreateNode<MapIR>(container->ast(), parent_op,
                                                   std::vector<ColumnExpression>{
                                                     ColumnExpression(
                                                       col_names.first, metadata_expr),
                                                     ColumnExpression(
                                                       col_names.second, fallback_expr)},
                                                   true));
      LOG(INFO) << "map_ir: " << map_ir->DebugString();
      PX_RETURN_IF_ERROR(container_op->ReplaceParent(parent_op, map_ir));

      PX_RETURN_IF_ERROR(PropagateTypeChangesFromNode(graph, map_ir, compiler_state_));
    }
  }

  return Status::OK();
}

Status ConvertMetadataRule::UpdateMetadataContainer(IRNode* container, MetadataIR* metadata,
                                                    ExpressionIR* metadata_expr) const {
  if (Match(container, Func())) {
    auto func = static_cast<FuncIR*>(container);
    LOG(INFO) << "Updating metadata expression in func" << metadata_expr->DebugString();
    PX_RETURN_IF_ERROR(func->UpdateArg(metadata, metadata_expr));
    return Status::OK();
  }
  if (Match(container, Map())) {
    auto map = static_cast<MapIR*>(container);
    for (const auto& expr : map->col_exprs()) {
      if (expr.node == metadata) {
        LOG(INFO) << "Updating metadata expression in map " << metadata_expr->DebugString();
        PX_RETURN_IF_ERROR(map->UpdateColExpr(expr.name, metadata_expr));
      }
    }
    return Status::OK();
  }
  if (Match(container, Filter())) {
    auto filter = static_cast<FilterIR*>(container);
    LOG(INFO) << "Updating metadata expression in filter" << metadata_expr->DebugString();
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
  return parent_type->HasColumn("time_") && parent_type->HasColumn("local_addr") &&
         func_name == "upid_to_pod_name";
}

StatusOr<bool> ConvertMetadataRule::Apply(IRNode* ir_node) {
  LOG(INFO) << "Running";
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

  auto resolved_table_type = parent->resolved_table_type();
  PX_ASSIGN_OR_RETURN(std::string key_column_name,
                      FindKeyColumn(resolved_table_type, md_property, ir_node));

  PX_ASSIGN_OR_RETURN(ColumnIR * key_column,
                      graph->CreateNode<ColumnIR>(ir_node->ast(), key_column_name, parent_op_idx));

  PX_ASSIGN_OR_RETURN(std::string func_name, md_property->UDFName(key_column_name));
  PX_ASSIGN_OR_RETURN(
      FuncIR * conversion_func,
      graph->CreateNode<FuncIR>(ir_node->ast(), FuncIR::Op{FuncIR::Opcode::non_op, "", func_name},
                                std::vector<ExpressionIR*>{key_column}));
  FuncIR* orig_conversion_func = conversion_func;
  ExpressionIR* col_conversion_func = static_cast<ExpressionIR*>(conversion_func);

  // TODO(ddelnano): Until the short lived process issue (gh#1638) is resolved, use a backup UDF via
  // local_addr in case the upid_to_pod_name fails to resolve the pod name
  FuncIR* backup_conversion_func = nullptr;
  std::pair<std::string, std::string> uniq_pod_name_cols;
  auto backup_conversion_available = CheckBackupConversionAvailable(resolved_table_type, func_name);
  if (backup_conversion_available) {
    absl::flat_hash_set<std::string> used_column_names;
    uniq_pod_name_cols = std::make_pair(GetUniquePodNameCol(resolved_table_type, &used_column_names), GetUniquePodNameCol(resolved_table_type, &used_column_names));
    PX_ASSIGN_OR_RETURN(ColumnIR * local_addr_col,
                        graph->CreateNode<ColumnIR>(ir_node->ast(), "local_addr", parent_op_idx));
    PX_ASSIGN_OR_RETURN(ColumnIR * time_col,
                        graph->CreateNode<ColumnIR>(ir_node->ast(), "time_", parent_op_idx));
    PX_ASSIGN_OR_RETURN(
        ColumnIR * pod_name_col,
        graph->CreateNode<ColumnIR>(ir_node->ast(), uniq_pod_name_cols.first, parent_op_idx));
    PX_ASSIGN_OR_RETURN(
        FuncIR * ip_conversion_func,
        graph->CreateNode<FuncIR>(ir_node->ast(),
                                  FuncIR::Op{FuncIR::Opcode::non_op, "", "_ip_to_pod_id_pem_exec"},
                                  std::vector<ExpressionIR*>{local_addr_col, time_col}));

    // This doesn't need to have a "pem exec" equivalent function as long as the metadata
    // annotation is set as done below
    PX_ASSIGN_OR_RETURN(
        backup_conversion_func,
        graph->CreateNode<FuncIR>(
            ir_node->ast(), FuncIR::Op{FuncIR::Opcode::non_op, "", "pod_id_to_pod_name"},
            std::vector<ExpressionIR*>{static_cast<ExpressionIR*>(ip_conversion_func)}));

    backup_conversion_func->set_annotations(ExpressionIR::Annotations(md_type));

    auto empty_string = static_cast<ExpressionIR*>(
        graph->CreateNode<StringIR>(ir_node->ast(), "").ConsumeValueOrDie());
    PX_ASSIGN_OR_RETURN(
        FuncIR * select_expr,
        graph->CreateNode<FuncIR>(
            ir_node->ast(), FuncIR::Op{FuncIR::Opcode::eq, "==", "equal"},
            std::vector<ExpressionIR*>{static_cast<ExpressionIR*>(pod_name_col), empty_string}));
    PX_ASSIGN_OR_RETURN(FuncIR * select_func,
                        graph->CreateNode<FuncIR>(
                            ir_node->ast(), FuncIR::Op{FuncIR::Opcode::non_op, "", "select"},
                            std::vector<ExpressionIR*>{static_cast<ExpressionIR*>(select_expr),
                                                       backup_conversion_func, pod_name_col}));

    conversion_func = select_func;
    PX_ASSIGN_OR_RETURN(col_conversion_func, graph->CreateNode<ColumnIR>(
                ir_node->ast(), uniq_pod_name_cols.second, parent_op_idx));
  }

  for (int64_t parent_id : graph->dag().ParentsOf(metadata->id())) {
    // For each container node of the metadata expression, update it to point to the
    // new conversion func instead.
    auto container = graph->Get(parent_id);
    PX_RETURN_IF_ERROR(UpdateMetadataContainer(container, metadata, col_conversion_func));
    if (backup_conversion_available) {
      PX_RETURN_IF_ERROR(AddOptimisticPodNameConversionMap(graph, container, orig_conversion_func,
                                                           conversion_func, uniq_pod_name_cols));
    }
  }

  // Propagate type changes from the new conversion_func.
  PX_RETURN_IF_ERROR(PropagateTypeChangesFromNode(graph, orig_conversion_func, compiler_state_));

  DCHECK_EQ(orig_conversion_func->EvaluatedDataType(), column_type)
      << "Expected the parent key column type and metadata property type to match.";
  orig_conversion_func->set_annotations(ExpressionIR::Annotations(md_type));
  LOG(INFO) << graph->DebugString();
  return true;
}

}  // namespace compiler
}  // namespace planner
}  // namespace carnot
}  // namespace px
