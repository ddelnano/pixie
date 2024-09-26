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
  StatusOr<MemorySourceIR*> FindRootMemSrc(IRNode* node) {
    /* auto parents = node->parents(); */
    /* while (parents.size() > 0) { */
    /*   node = parents[0]; */
    /*   parents = node->parents(); */
    /* } */
    auto graph = node->graph();
    auto node_id = node->id();
    auto parents = graph->dag().ParentsOf(node_id);
    /* LOG(INFO) << "Parents size: " << parents.size(); */
    while (parents.size() > 0) {
      node_id = parents[0];
      /* LOG(INFO) << "Node debug string: " << graph->Get(node_id)->DebugString(); */
      parents = graph->dag().ParentsOf(node_id);
    }
    auto root_node = graph->Get(node_id);
    if (Match(root_node, MemorySource())) {
      return static_cast<MemorySourceIR*>(root_node);
    }
    return error::Internal("Could not find root memory source for node: $0", node->DebugString());
  }
}

std::string ConvertMetadataRule::GetUniquePodNameCol(std::shared_ptr<TableType> parent_type) {
  do {
    auto col_name_counter = 0;
    auto new_col = absl::StrCat("pod_name_", col_name_counter++);
    if (!parent_type->HasColumn(new_col)) {
      return new_col;
    }
  } while (true);
}

Status ConvertMetadataRule::AddMetadataMapToRootAncestor(
    IR* graph, std::vector<int64_t> parents_in, const std::pair<std::string, std::string>& col_names,
    ExpressionIR* metadata_expr, ExpressionIR* fallback_expr, std::string func_name) {
  auto root_id = parents_in[0];
  auto parents = graph->dag().ParentsOf(parents_in[0]);
  while (parents.size() > 0) {
    root_id = parents[0];
    parents = graph->dag().ParentsOf(root_id);
  }
  auto root_node = graph->Get(root_id);
  DCHECK(Match(root_node, Operator()));

  auto root_op = static_cast<OperatorIR*>(graph->Get(root_id));
  /* if (visited_root_ops_.find(root_op) != visited_root_ops_.end()) { */
  /*   return Status::OK(); */
  /* } */

  auto table_type = root_op->resolved_table_type();
  PX_ASSIGN_OR_RETURN(
      auto map_ir,
      graph->CreateNode<MapIR>(
          root_node->ast(), root_op,
          std::vector<ColumnExpression>{ColumnExpression(col_names.first, metadata_expr)}, true));
  PX_ASSIGN_OR_RETURN(
      auto child_map_ir,
      graph->CreateNode<MapIR>(
          root_node->ast(), static_cast<OperatorIR*>(map_ir),
          std::vector<ColumnExpression>{ColumnExpression(col_names.second, fallback_expr)}, true));
  for (int64_t dep_id : graph->dag().DependenciesOf(root_id)) {
    if (dep_id == map_ir->id()) {
      continue;
    }
    if (Match(graph->Get(dep_id), Operator())) {
      auto dep_op = static_cast<OperatorIR*>(graph->Get(dep_id));
      PX_RETURN_IF_ERROR(dep_op->ReplaceParent(root_op, child_map_ir));
    }
  }
  applied_md_exprs_.insert({std::make_pair(func_name, root_op), col_names.second});

  PX_RETURN_IF_ERROR(PropagateTypeChangesFromNode(graph, map_ir, compiler_state_));
  PX_RETURN_IF_ERROR(PropagateTypeChangesFromNode(graph, child_map_ir, compiler_state_));
  return Status::OK();
}

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
  return parent_type->HasColumn("time_") && parent_type->HasColumn("local_addr") &&
         func_name == "upid_to_pod_name";
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
  /* LOG(INFO) << "Applying ConvertMetadataRule to node: " << ir_node->DebugString(); */
  /* LOG(INFO) << "Graph before: " << graph->DebugString(); */

  PX_ASSIGN_OR_RETURN(auto parent, metadata->ReferencedOperator());
  PX_ASSIGN_OR_RETURN(auto containing_ops, metadata->ContainingOperators());

  auto resolved_table_type = parent->resolved_table_type();
  PX_ASSIGN_OR_RETURN(std::string key_column_name,
                      FindKeyColumn(resolved_table_type, md_property, ir_node));

  PX_ASSIGN_OR_RETURN(ColumnIR * key_column,
                      graph->CreateNode<ColumnIR>(ir_node->ast(), key_column_name, parent_op_idx));

  PX_ASSIGN_OR_RETURN(std::string func_name, md_property->UDFName(key_column_name));
  auto backup_conversion_available =
      CheckBackupConversionAvailable(parent->resolved_table_type(), func_name);

  PX_ASSIGN_OR_RETURN(auto parent_mem_src, FindRootMemSrc(metadata));
  /* LOG(INFO) << "Parent mem src: " << parent_mem_src->DebugString(); */
  /* LOG(INFO) << "applied_md_exprs_ size: " << applied_md_exprs_.size(); */
  // Reuse the conversion function if it has already been applied to the metadata expression.
  auto applied_md_exprs_key = std::make_pair(func_name, static_cast<OperatorIR*>(parent_mem_src));
  if (backup_conversion_available && applied_md_exprs_.find(applied_md_exprs_key) != applied_md_exprs_.end()) {
    for (int64_t parent_id : graph->dag().ParentsOf(metadata->id())) {
      PX_ASSIGN_OR_RETURN(auto md_expr, graph->CreateNode<ColumnIR>(
                                                 ir_node->ast(), applied_md_exprs_[applied_md_exprs_key], parent_op_idx));
      PX_RETURN_IF_ERROR(UpdateMetadataContainer(graph->Get(parent_id), metadata, md_expr));
      md_expr->set_annotations(ExpressionIR::Annotations(md_type));
    }
    /* LOG(INFO) << "Applied already applied md conversion: " << graph->DebugString(); */
    return true;
  }
  PX_ASSIGN_OR_RETURN(
      FuncIR * conversion_func,
      graph->CreateNode<FuncIR>(ir_node->ast(), FuncIR::Op{FuncIR::Opcode::non_op, "", func_name},
                                std::vector<ExpressionIR*>{key_column}));
  FuncIR* orig_conversion_func = conversion_func;
  ExpressionIR* col_conversion_func = static_cast<ExpressionIR*>(conversion_func);

  // TODO(ddelnano): Until the short lived process issue (gh#1638) is resolved, use a backup UDF via
  // local_addr in case the upid_to_pod_name fails to resolve the pod name
  std::pair<std::string, std::string> col_names;
  FuncIR* backup_conversion_func = nullptr;
  FuncIR* ip_conversion_func = nullptr;
  if (backup_conversion_available) {
    col_names = std::make_pair(GetUniquePodNameCol(resolved_table_type),
                               GetUniquePodNameCol(resolved_table_type));

    PX_ASSIGN_OR_RETURN(ColumnIR * local_addr_col,
                        graph->CreateNode<ColumnIR>(ir_node->ast(), "local_addr", parent_op_idx));
    PX_ASSIGN_OR_RETURN(ColumnIR * time_col,
                        graph->CreateNode<ColumnIR>(ir_node->ast(), "time_", parent_op_idx));
    PX_ASSIGN_OR_RETURN(
        ColumnIR* pod_name_col,
        graph->CreateNode<ColumnIR>(ir_node->ast(), col_names.first, parent_op_idx));

    PX_ASSIGN_OR_RETURN(
        ip_conversion_func,
        graph->CreateNode<FuncIR>(ir_node->ast(),
                                  FuncIR::Op{FuncIR::Opcode::non_op, "", "_ip_to_pod_id_pem_exec"},
                                  std::vector<ExpressionIR*>{local_addr_col, time_col}));
    PX_ASSIGN_OR_RETURN(
        backup_conversion_func,
        graph->CreateNode<FuncIR>(
            ir_node->ast(), FuncIR::Op{FuncIR::Opcode::non_op, "", "_pod_id_to_pod_name_pem_exec"},
            std::vector<ExpressionIR*>{static_cast<ExpressionIR*>(ip_conversion_func)}));
    auto empty_string = static_cast<ExpressionIR*>(
        graph->CreateNode<StringIR>(ir_node->ast(), "").ConsumeValueOrDie());
    PX_ASSIGN_OR_RETURN(
        FuncIR * select_expr,
        graph->CreateNode<FuncIR>(
            ir_node->ast(), FuncIR::Op{FuncIR::Opcode::eq, "==", "equal"},
            std::vector<ExpressionIR*>{static_cast<ExpressionIR*>(pod_name_col), empty_string}));
    PX_ASSIGN_OR_RETURN(auto second_func, graph->CopyNode<ColumnIR>(pod_name_col));
    PX_ASSIGN_OR_RETURN(FuncIR * select_func,
                        graph->CreateNode<FuncIR>(
                            ir_node->ast(), FuncIR::Op{FuncIR::Opcode::non_op, "", "select"},
                            std::vector<ExpressionIR*>{static_cast<ExpressionIR*>(select_expr),
                                                       backup_conversion_func, second_func}));

    conversion_func = select_func;
    PX_ASSIGN_OR_RETURN(col_conversion_func, graph->CreateNode<ColumnIR>(
                                                 ir_node->ast(), col_names.second, parent_op_idx));
  }

  auto md_parents = graph->dag().ParentsOf(metadata->id());
  if (orig_conversion_func != conversion_func && md_parents.size() > 0) {
    for (int64_t parent_id : md_parents) {
      /* LOG(INFO) << "Adding map to Parent id ancestors: " << parent_id; */
      PX_RETURN_IF_ERROR(AddMetadataMapToRootAncestor(graph, {parent_id}, col_names, orig_conversion_func,
                                                      conversion_func, func_name));
    }
    /* PX_RETURN_IF_ERROR(AddMetadataMapToRootAncestor(graph, md_parents, col_names, */
    /*                                                 orig_conversion_func, conversion_func)); */
  }
  for (int64_t parent_id : md_parents) {
    // For each container node of the metadata expression, update it to point to the
    // new conversion func instead.
    PX_RETURN_IF_ERROR(
        UpdateMetadataContainer(graph->Get(parent_id), metadata, col_conversion_func));
    PX_RETURN_IF_ERROR(PropagateTypeChangesFromNode(graph, graph->Get(parent_id), compiler_state_));
  }

  // Propagate type changes from the new conversion_func.
  PX_RETURN_IF_ERROR(PropagateTypeChangesFromNode(graph, conversion_func, compiler_state_));

  DCHECK_EQ(conversion_func->EvaluatedDataType(), column_type)
      << "Expected the parent key column type and metadata property type to match.";
  if (backup_conversion_available) {
    /* ip_conversion_func->set_annotations(ExpressionIR::Annotations(md_type)); */
    /* backup_conversion_func->set_annotations(ExpressionIR::Annotations(md_type)); */
    col_conversion_func->set_annotations(ExpressionIR::Annotations(md_type));
  }
  orig_conversion_func->set_annotations(ExpressionIR::Annotations(md_type));
  /* LOG(INFO) << "After conversion applied_md_exprs_ size: " << applied_md_exprs_.size(); */
  /* LOG(INFO) << "Graph after md conversion: " << graph->DebugString(); */
  return true;
}

}  // namespace compiler
}  // namespace planner
}  // namespace carnot
}  // namespace px
