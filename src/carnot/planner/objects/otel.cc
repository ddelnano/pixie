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

#include "src/carnot/planner/objects/otel.h"
#include <absl/container/flat_hash_set.h>

#include <functional>
#include <utility>
#include <vector>

#include "src/carnot/planner/ir/otel_export_sink_ir.h"
#include "src/carnot/planner/objects/dataframe.h"
#include "src/carnot/planner/objects/dict_object.h"
#include "src/carnot/planner/objects/exporter.h"
#include "src/carnot/planner/objects/expr_object.h"
#include "src/carnot/planner/objects/funcobject.h"
#include "src/carnot/planner/objects/none_object.h"
#include "src/carnot/planpb/plan.pb.h"
#include "src/shared/types/typespb/types.pb.h"

namespace px {
namespace carnot {
namespace planner {
namespace compiler {

StatusOr<std::shared_ptr<OTelModule>> OTelModule::Create(CompilerState* compiler_state,
                                                         ASTVisitor* ast_visitor, IR* ir) {
  auto otel_module = std::shared_ptr<OTelModule>(new OTelModule(ast_visitor));
  PL_RETURN_IF_ERROR(otel_module->Init(compiler_state, ir));
  return otel_module;
}

StatusOr<std::shared_ptr<OTelMetrics>> OTelMetrics::Create(ASTVisitor* ast_visitor, IR* graph) {
  auto otel_metrics = std::shared_ptr<OTelMetrics>(new OTelMetrics(ast_visitor, graph));
  PL_RETURN_IF_ERROR(otel_metrics->Init());
  return otel_metrics;
}

StatusOr<std::shared_ptr<EndpointConfig>> EndpointConfig::Create(
    ASTVisitor* ast_visitor, std::string url,
    std::vector<EndpointConfig::ConnAttribute> attributes) {
  return std::shared_ptr<EndpointConfig>(
      new EndpointConfig(ast_visitor, std::move(url), std::move(attributes)));
}

Status ExportToOTel(const OTelData& data, const pypa::AstPtr& ast, Dataframe* df) {
  auto op = df->op();
  return op->graph()->CreateNode<OTelExportSinkIR>(ast, op, data).status();
}

StatusOr<std::string> GetArgAsString(const pypa::AstPtr& ast, const ParsedArgs& args,
                                     std::string_view arg_name) {
  PL_ASSIGN_OR_RETURN(StringIR * arg_ir, GetArgAs<StringIR>(ast, args, arg_name));
  return arg_ir->str();
}

Status ParseEndpointConfig(CompilerState* compiler_state, const QLObjectPtr& endpoint,
                           planpb::OTelEndpointConfig* pb) {
  if (NoneObject::IsNoneObject(endpoint)) {
    if (!compiler_state->endpoint_config()) {
      return endpoint->CreateError("no default config found for endpoint, please specify one");
    }

    *pb = *compiler_state->endpoint_config();
    return Status::OK();
  }
  // TODO(philkuz) determine how to handle a default configuration based on the plugin.
  if (endpoint->type() != EndpointConfig::EndpointType.type()) {
    return endpoint->CreateError("expected Endpoint type for 'endpoint' arg, received $0",
                                 endpoint->name());
  }

  return static_cast<EndpointConfig*>(endpoint.get())->ToProto(pb);
}

StatusOr<std::shared_ptr<OTelDataContainer>> OTelDataContainer::Create(
    ASTVisitor* ast_visitor, std::variant<OTelMetric, OTelSpan> data) {
  return std::shared_ptr<OTelDataContainer>(new OTelDataContainer(ast_visitor, std::move(data)));
}

StatusOr<std::vector<OTelAttribute>> ParseAttributes(DictObject* attributes) {
  auto values = attributes->values();
  auto keys = attributes->keys();
  DCHECK_EQ(values.size(), keys.size());
  std::vector<OTelAttribute> otel_attributes;
  for (const auto& [idx, keyobj] : Enumerate(keys)) {
    PL_ASSIGN_OR_RETURN(auto key, GetArgAs<StringIR>(keyobj, "attribute"));
    PL_ASSIGN_OR_RETURN(auto val, GetArgAs<ColumnIR>(values[idx], "attribute value column"));
    otel_attributes.push_back({key->str(), val});
  }
  return otel_attributes;
}

StatusOr<QLObjectPtr> GaugeDefinition(IR* graph, const pypa::AstPtr& ast, const ParsedArgs& args,
                                      ASTVisitor* visitor) {
  OTelMetric metric;
  PL_ASSIGN_OR_RETURN(metric.name, GetArgAsString(ast, args, "name"));
  PL_ASSIGN_OR_RETURN(metric.description, GetArgAsString(ast, args, "description"));
  // We add the time_ column  automatically.
  PL_ASSIGN_OR_RETURN(metric.time_column,
                      graph->CreateNode<ColumnIR>(ast, "time_", /* parent_op_idx */ 0));

  PL_ASSIGN_OR_RETURN(auto val, GetArgAs<ColumnIR>(ast, args, "value"));
  metric.unit_column = val;
  metric.metric = OTelMetricGauge{val};

  QLObjectPtr attributes = args.GetArg("attributes");
  if (!DictObject::IsDict(attributes)) {
    return attributes->CreateError("Expected attributes to be a dictionary, received $0",
                                   attributes->name());
  }

  PL_ASSIGN_OR_RETURN(metric.attributes,
                      ParseAttributes(static_cast<DictObject*>(attributes.get())));

  return OTelDataContainer::Create(visitor, std::move(metric));
}

StatusOr<QLObjectPtr> SummaryDefinition(IR* graph, const pypa::AstPtr& ast, const ParsedArgs& args,
                                        ASTVisitor* visitor) {
  OTelMetric metric;
  PL_ASSIGN_OR_RETURN(metric.name, GetArgAsString(ast, args, "name"));
  PL_ASSIGN_OR_RETURN(metric.description, GetArgAsString(ast, args, "description"));
  // We add the time_ column  automatically.
  PL_ASSIGN_OR_RETURN(metric.time_column,
                      graph->CreateNode<ColumnIR>(ast, "time_", /* parent_op_idx */ 0));

  OTelMetricSummary summary;
  PL_ASSIGN_OR_RETURN(summary.count_column, GetArgAs<ColumnIR>(ast, args, "count"));
  PL_ASSIGN_OR_RETURN(summary.sum_column, GetArgAs<ColumnIR>(ast, args, "sum"));

  auto qvs = args.GetArg("quantile_values");
  if (!DictObject::IsDict(qvs)) {
    return qvs->CreateError("Expected quantile_values to be a dictionary, received $0",
                            qvs->name());
  }
  auto dict = static_cast<DictObject*>(qvs.get());
  auto values = dict->values();
  auto keys = dict->keys();
  CHECK_EQ(values.size(), keys.size());
  if (keys.empty()) {
    return qvs->CreateError("Summary must have at least one quantile value specified");
  }
  for (const auto& [idx, keyobj] : Enumerate(keys)) {
    PL_ASSIGN_OR_RETURN(auto quantile, GetArgAs<FloatIR>(keyobj, "quantile"));
    PL_ASSIGN_OR_RETURN(auto val, GetArgAs<ColumnIR>(values[idx], "quantile value column"));
    summary.quantiles.push_back({quantile->val(), val});
  }
  metric.unit_column = summary.quantiles[0].value_column;
  metric.metric = summary;

  QLObjectPtr attributes = args.GetArg("attributes");
  if (!DictObject::IsDict(attributes)) {
    return attributes->CreateError("Expected attributes to be a dictionary, received $0",
                                   attributes->name());
  }

  PL_ASSIGN_OR_RETURN(metric.attributes,
                      ParseAttributes(static_cast<DictObject*>(attributes.get())));

  return OTelDataContainer::Create(visitor, std::move(metric));
}

StatusOr<QLObjectPtr> OTelDataDefinition(CompilerState* compiler_state, const pypa::AstPtr&,
                                         const ParsedArgs& args, ASTVisitor* visitor) {
  OTelData otel_data;
  PL_RETURN_IF_ERROR(
      ParseEndpointConfig(compiler_state, args.GetArg("endpoint"), &otel_data.endpoint_config));
  QLObjectPtr data = args.GetArg("data");
  if (!CollectionObject::IsCollection(data)) {
    return data->CreateError("Expected data to be a collection, received $0", data->name());
  }

  auto data_attr = static_cast<CollectionObject*>(data.get());
  if (data_attr->items().empty()) {
    return data_attr->CreateError("Must specify at least 1 data configuration");
  }
  for (const auto& data : data_attr->items()) {
    if (!OTelDataContainer::IsOTelDataContainer(data)) {
      return data->CreateError("Expected an OTelDataContainer, received $0", data->name());
    }
    auto container = static_cast<OTelDataContainer*>(data.get());

    std::visit(overload{
                   [&otel_data](const OTelMetric& metric) { otel_data.metrics.push_back(metric); },
                   [&otel_data](const OTelSpan& span) { otel_data.spans.push_back(span); },
               },
               container->data());
  }

  QLObjectPtr resource = args.GetArg("resource");
  if (!DictObject::IsDict(resource)) {
    return resource->CreateError("Expected resource to be a dictionary, received $0",
                                 resource->name());
  }

  PL_ASSIGN_OR_RETURN(otel_data.resource_attributes,
                      ParseAttributes(static_cast<DictObject*>(resource.get())));
  bool has_service_name = false;
  for (const auto& attribute : otel_data.resource_attributes) {
    has_service_name = (attribute.name == "service.name");
    if (has_service_name) break;
  }

  if (!has_service_name) {
    return resource->CreateError("'service.name' must be specified in resource");
  }

  return Exporter::Create(visitor, [otel_data](auto&& ast, auto&& df) -> Status {
    return ExportToOTel(otel_data, std::forward<decltype(ast)>(ast),
                        std::forward<decltype(df)>(df));
  });
}

StatusOr<QLObjectPtr> EndpointConfigDefinition(const pypa::AstPtr& ast, const ParsedArgs& args,
                                               ASTVisitor* visitor) {
  PL_ASSIGN_OR_RETURN(auto url, GetArgAsString(ast, args, "url"));

  std::vector<EndpointConfig::ConnAttribute> attributes;
  auto headers = args.GetArg("headers");
  if (!DictObject::IsDict(headers)) {
    return headers->CreateError("expected dict() for 'attributes' arg, received $0",
                                headers->name());
  }
  auto headers_dict = static_cast<DictObject*>(headers.get());
  for (const auto& [i, key] : Enumerate(headers_dict->keys())) {
    PL_ASSIGN_OR_RETURN(StringIR * key_ir, GetArgAs<StringIR>(ast, key, "header key"));
    PL_ASSIGN_OR_RETURN(StringIR * val_ir,
                        GetArgAs<StringIR>(ast, headers_dict->values()[i], "header value"));
    attributes.push_back(EndpointConfig::ConnAttribute{key_ir->str(), val_ir->str()});
  }
  return EndpointConfig::Create(visitor, url, attributes);
}

Status OTelModule::Init(CompilerState* compiler_state, IR* ir) {
  // Setup methods.
  PL_ASSIGN_OR_RETURN(
      std::shared_ptr<FuncObject> data_fn,
      FuncObject::Create(kDataOpID, {"resource", "data", "endpoint"}, {{"endpoint", "None"}},
                         /* has_variable_len_args */ false,
                         /* has_variable_len_kwargs */ false,
                         std::bind(&OTelDataDefinition, compiler_state, std::placeholders::_1,
                                   std::placeholders::_2, std::placeholders::_3),
                         ast_visitor()));
  PL_RETURN_IF_ERROR(data_fn->SetDocString(kDataOpDocstring));
  AddMethod(kDataOpID, data_fn);

  PL_ASSIGN_OR_RETURN(auto metric, OTelMetrics::Create(ast_visitor(), ir));
  PL_RETURN_IF_ERROR(AssignAttribute("metric", metric));

  PL_ASSIGN_OR_RETURN(std::shared_ptr<FuncObject> endpoint_fn,
                      FuncObject::Create(kEndpointOpID, {"url", "headers"}, {{"headers", "{}"}},
                                         /* has_variable_len_args */ false,
                                         /* has_variable_len_kwargs */ false,
                                         std::bind(&EndpointConfigDefinition, std::placeholders::_1,
                                                   std::placeholders::_2, std::placeholders::_3),
                                         ast_visitor()));

  AddMethod(kEndpointOpID, endpoint_fn);
  PL_RETURN_IF_ERROR(endpoint_fn->SetDocString(kEndpointOpDocstring));
  return Status::OK();

  return Status::OK();
}

Status OTelMetrics::Init() {
  // Setup methods.
  PL_ASSIGN_OR_RETURN(std::shared_ptr<FuncObject> gauge_fn,
                      FuncObject::Create(kGaugeOpID, {"name", "value", "description", "attributes"},
                                         {{"description", "\"\""}, {"attributes", "{}"}},
                                         /* has_variable_len_args */ false,
                                         /* has_variable_len_kwargs */ false,
                                         std::bind(&GaugeDefinition, graph_, std::placeholders::_1,
                                                   std::placeholders::_2, std::placeholders::_3),
                                         ast_visitor()));
  PL_RETURN_IF_ERROR(gauge_fn->SetDocString(kGaugeOpDocstring));
  AddMethod(kGaugeOpID, gauge_fn);

  PL_ASSIGN_OR_RETURN(
      std::shared_ptr<FuncObject> summary_fn,
      FuncObject::Create(kSummaryOpID,
                         {"name", "count", "sum", "quantile_values", "description", "attributes"},
                         {{"description", "\"\""}, {"attributes", "{}"}},
                         /* has_variable_len_args */ false,
                         /* has_variable_len_kwargs */ false,
                         std::bind(&SummaryDefinition, graph_, std::placeholders::_1,
                                   std::placeholders::_2, std::placeholders::_3),
                         ast_visitor()));
  PL_RETURN_IF_ERROR(summary_fn->SetDocString(kSummaryOpDocstring));
  AddMethod(kSummaryOpID, summary_fn);

  return Status::OK();
}

Status EndpointConfig::ToProto(planpb::OTelEndpointConfig* pb) {
  pb->set_url(url_);
  for (const auto& attr : attributes_) {
    (*pb->mutable_headers())[attr.name] = attr.value;
  }
  return Status::OK();
}

}  // namespace compiler
}  // namespace planner
}  // namespace carnot
}  // namespace px