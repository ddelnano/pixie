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

#include "src/stirling/source_connectors/dynamic_object/dynamic_object_connector.h"

#include <sys/stat.h>
#include <unistd.h>

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include "src/common/base/base.h"
#include "src/common/fs/fs_wrapper.h"
#include "src/stirling/core/canonical_types.h"

namespace px {
namespace stirling {

namespace {

void FlattenOIJsonRecursive(const rapidjson::Value& node, const std::string& parent_path,
                            uint64_t timestamp_ns, md::UPID upid, DataTable* traces_table,
                            DataTable* stats_table) {
  if (!node.IsObject()) {
    return;
  }

  // Get node name.
  std::string name;
  if (node.HasMember("name") && node["name"].IsString()) {
    name = node["name"].GetString();
  }

  // Build the semicolon-delimited path for flamegraph (folded stack trace format).
  std::string stack_path;
  if (parent_path.empty()) {
    stack_path = name.empty() ? "(root)" : name;
  } else if (!name.empty()) {
    stack_path = absl::StrCat(parent_path, ";", name);
  } else {
    stack_path = parent_path;
  }

  // Build the dot-delimited path for container stats.
  // Convert semicolons to dots for the container path.
  std::string dot_path = stack_path;
  std::replace(dot_path.begin(), dot_path.end(), ';', '.');

  int64_t static_size = 0;
  int64_t dynamic_size = 0;
  int64_t padding_savings = 0;

  if (node.HasMember("staticSize") && node["staticSize"].IsInt64()) {
    static_size = node["staticSize"].GetInt64();
  }
  if (node.HasMember("dynamicSize") && node["dynamicSize"].IsInt64()) {
    dynamic_size = node["dynamicSize"].GetInt64();
  }
  if (node.HasMember("paddingSavingsSize") && node["paddingSavingsSize"].IsInt64()) {
    padding_savings = node["paddingSavingsSize"].GetInt64();
  }

  int64_t total_size = static_size + dynamic_size;

  // Emit row to object_memory_traces table.
  {
    DataTable::RecordBuilder<&kObjectMemoryTracesTable> r(traces_table, timestamp_ns);
    r.Append<r.ColIndex("time_")>(timestamp_ns);
    r.Append<r.ColIndex("upid")>(upid.value());
    r.Append<r.ColIndex("stack_trace")>(stack_path);
    r.Append<r.ColIndex("count")>(total_size);
    r.Append<r.ColIndex("padding_savings")>(padding_savings);
  }

  // If this node has a "length" field, it's a container — emit to stats table.
  if (node.HasMember("length") && node["length"].IsInt64()) {
    std::string type_name;
    if (node.HasMember("typeName") && node["typeName"].IsString()) {
      type_name = node["typeName"].GetString();
    }
    int64_t length = node["length"].GetInt64();

    DataTable::RecordBuilder<&kObjectContainerStatsTable> r(stats_table, timestamp_ns);
    r.Append<r.ColIndex("time_")>(timestamp_ns);
    r.Append<r.ColIndex("upid")>(upid.value());
    r.Append<r.ColIndex("container_path")>(dot_path);
    r.Append<r.ColIndex("container_type")>(type_name);
    r.Append<r.ColIndex("element_count")>(length);
    r.Append<r.ColIndex("total_size")>(total_size);
  }

  // Recurse into members.
  if (node.HasMember("members") && node["members"].IsArray()) {
    for (const auto& member : node["members"].GetArray()) {
      FlattenOIJsonRecursive(member, stack_path, timestamp_ns, upid, traces_table, stats_table);
    }
  }
}

}  // namespace

DynamicObjectTraceConnector::DynamicObjectTraceConnector(std::string_view name,
                                                         ArrayView<DataTableSchema> table_schemas,
                                                         uint32_t target_pid,
                                                         std::chrono::nanoseconds ttl,
                                                         int32_t required_count)
    : SourceConnector(name, table_schemas),
      target_pid_(target_pid),
      ttl_(ttl),
      required_count_(required_count) {}

StatusOr<std::unique_ptr<SourceConnector>> DynamicObjectTraceConnector::Create(
    std::string_view name, dynamic_tracing::ir::logical::TracepointDeployment* program) {
  // Verify we have a deployment_spec with upid_list.
  if (!program->has_deployment_spec()) {
    return error::InvalidArgument("DynamicObjectTraceConnector requires a deployment_spec.");
  }

  const auto& deployment_spec = program->deployment_spec();
  if (!deployment_spec.has_upid_list()) {
    return error::InvalidArgument(
        "DynamicObjectTraceConnector requires upid_list in deployment_spec.");
  }

  const auto& upid_list = deployment_spec.upid_list();
  if (upid_list.upids_size() == 0) {
    return error::InvalidArgument("upid_list must contain at least one UPID.");
  }

  // For now, only support a single UPID.
  if (upid_list.upids_size() > 1) {
    return error::InvalidArgument(
        "DynamicObjectTraceConnector currently only supports a single UPID.");
  }

  uint32_t target_pid = upid_list.upids(0).pid();
  if (target_pid == 0) {
    return error::InvalidArgument("UPID pid must be non-zero.");
  }

  // Get TTL from the program. Duration is stored as seconds + nanos.
  int64_t ttl_ns = 0;
  if (program->has_ttl()) {
    ttl_ns = program->ttl().seconds() * 1000000000LL + program->ttl().nanos();
  }
  auto ttl = std::chrono::nanoseconds(ttl_ns);

  // Get required count from the program.
  int32_t required_count = program->count();
  if (required_count <= 0) {
    required_count = 1;  // Default to 1 if not specified.
  }

  LOG(INFO) << absl::Substitute(
      "Creating DynamicObjectTraceConnector: name=$0, target_pid=$1, ttl=$2ns, count=$3", name,
      target_pid, ttl.count(), required_count);

  std::unique_ptr<SourceConnector> connector(new DynamicObjectTraceConnector(
      name, ArrayView<DataTableSchema>(kTables.data(), kTables.size()), target_pid, ttl,
      required_count));

  return connector;
}

Status DynamicObjectTraceConnector::InitImpl() {
  // Initialize the frequency managers.
  sampling_freq_mgr_.set_period(kSamplingPeriod);
  push_freq_mgr_.set_period(kPushPeriod);

  start_time_ = std::chrono::steady_clock::now();

  // Verify the target process exists.
  if (!IsTargetProcessAlive()) {
    return error::NotFound("Target process $0 is not running.", target_pid_);
  }

  // Start the initial subprocess.
  return StartSubprocess();
}

Status DynamicObjectTraceConnector::StartSubprocess() {
  // Build the path to the target process's stdout.
  std::string stdout_path = absl::Substitute("/proc/$0/fd/1", target_pid_);

  // Check if the path exists and is readable.
  if (!fs::Exists(stdout_path)) {
    return error::NotFound("Cannot access target process stdout at $0", stdout_path);
  }

  subprocess_ = std::make_unique<SubProcess>();

  // Use cat to follow the target process's stdout.
  std::vector<std::string> args = {"/usr/bin/cat", stdout_path};

  LOG(INFO) << absl::Substitute("Starting subprocess: $0", absl::StrJoin(args, " "));

  PX_RETURN_IF_ERROR(subprocess_->Start(args, /*stderr_to_stdout=*/true));

  LOG(INFO) << absl::Substitute("Subprocess started with PID $0", subprocess_->child_pid());

  return Status::OK();
}

bool DynamicObjectTraceConnector::IsTargetProcessAlive() const {
  std::string proc_path = absl::Substitute("/proc/$0", target_pid_);
  return fs::Exists(proc_path);
}

bool DynamicObjectTraceConnector::IsTTLExceeded() const {
  if (ttl_.count() == 0) {
    return false;  // No TTL set, never expires.
  }

  auto elapsed = std::chrono::steady_clock::now() - start_time_;
  return elapsed >= ttl_;
}

void DynamicObjectTraceConnector::TransferDataImpl(ConnectorContext* ctx) {
  PX_UNUSED(ctx);

  if (trace_complete_) {
    return;
  }

  // Check if TTL has been exceeded.
  if (IsTTLExceeded()) {
    LOG(WARNING) << "TTL exceeded for DynamicObjectTraceConnector.";
    trace_complete_ = true;
    trace_status_ = error::DeadlineExceeded("TTL exceeded before completing required count.");
    if (subprocess_ && subprocess_->IsRunning()) {
      subprocess_->Kill();
    }
    return;
  }

  // Check if the target process is still alive.
  if (!IsTargetProcessAlive()) {
    LOG(INFO) << absl::Substitute("Target process $0 is no longer running.", target_pid_);
    trace_complete_ = true;
    if (successful_completions_ >= required_count_) {
      trace_status_ = Status::OK();
    } else {
      trace_status_ = error::Internal(
          "Target process exited before completing required count. Completed: $0, Required: $1",
          successful_completions_, required_count_);
    }
    if (subprocess_ && subprocess_->IsRunning()) {
      subprocess_->Kill();
    }
    return;
  }

  // Check if we have a subprocess running.
  if (!subprocess_) {
    auto status = StartSubprocess();
    if (!status.ok()) {
      LOG(ERROR) << "Failed to start subprocess: " << status.ToString();
      trace_complete_ = true;
      trace_status_ = status;
    }
    return;
  }

  // Log subprocess status for debugging.
  bool is_running = subprocess_->IsRunning();
  LOG_EVERY_N(INFO, 50) << absl::Substitute("TransferDataImpl: subprocess running=$0, pid=$1",
                                            is_running, subprocess_->child_pid());

  // Read any available output from the subprocess (drain it).
  std::string output;
  auto status = subprocess_->Stdout(&output);
  if (!status.ok()) {
    LOG(WARNING) << "Failed to read subprocess stdout: " << status.ToString();
  }

  // Generate mock OI JSON and flatten into tables.
  std::string mock_json;
  GenerateMockOIJson(&mock_json);

  rapidjson::Document doc;
  doc.Parse(mock_json.c_str());
  if (!doc.HasParseError() && doc.IsArray() && doc.Size() > 0) {
    uint64_t timestamp_ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count();
    md::UPID upid(/* asid */ 0, target_pid_, /* start_time_ticks */ 0);

    DataTable* traces_table = data_tables_[kTracesTableNum];
    DataTable* stats_table = data_tables_[kStatsTableNum];

    if (traces_table != nullptr && stats_table != nullptr) {
      FlattenOIJson(doc, timestamp_ns, upid, traces_table, stats_table);
    }
  }

  // Check if the subprocess is still running.
  if (!is_running) {
    int exit_status = subprocess_->Wait(/*close_pipe=*/false);

    // Read any remaining output.
    status = subprocess_->Stdout(&output);

    if (exit_status == 0) {
      successful_completions_++;
      LOG(INFO) << absl::Substitute("Subprocess completed successfully. Completions: $0/$1",
                                    successful_completions_, required_count_);

      if (successful_completions_ >= required_count_) {
        LOG(INFO) << "Required count reached. Trace complete.";
        trace_complete_ = true;
        trace_status_ = Status::OK();
        return;
      }

      // Relaunch the subprocess if the target is still alive.
      subprocess_.reset();
      if (IsTargetProcessAlive()) {
        auto start_status = StartSubprocess();
        if (!start_status.ok()) {
          LOG(ERROR) << "Failed to restart subprocess: " << start_status.ToString();
          trace_complete_ = true;
          trace_status_ = start_status;
        }
      }
    } else {
      LOG(ERROR) << absl::Substitute("Subprocess exited with non-zero status: $0", exit_status);
      trace_complete_ = true;
      trace_status_ =
          error::Internal("Subprocess exited with non-zero status: $0", exit_status);
    }
  }
}

void DynamicObjectTraceConnector::GenerateMockOIJson(std::string* output) {
  // Grow entries each call to simulate AddressBook growth.
  mock_entry_count_ += 2;

  rapidjson::Document doc;
  doc.SetArray();
  auto& alloc = doc.GetAllocator();

  // Root object: "this" -> AddressBook
  rapidjson::Value root(rapidjson::kObjectType);
  root.AddMember("name", "this", alloc);
  root.AddMember("typePath", "this", alloc);
  root.AddMember("typeName", "AddressBook", alloc);
  root.AddMember("isTypedef", false, alloc);
  root.AddMember("staticSize", 64, alloc);
  root.AddMember("paddingSavingsSize", 4, alloc);

  // Members array for AddressBook.
  rapidjson::Value members(rapidjson::kArrayType);

  // Member 1: rev (int, 4 bytes)
  {
    rapidjson::Value rev(rapidjson::kObjectType);
    rev.AddMember("name", "rev", alloc);
    rev.AddMember("typePath", "rev", alloc);
    rev.AddMember("typeName", "int", alloc);
    rev.AddMember("isTypedef", false, alloc);
    rev.AddMember("staticSize", 4, alloc);
    rev.AddMember("dynamicSize", 0, alloc);
    members.PushBack(rev, alloc);
  }

  // Member 2: Owner (string, 32 bytes static)
  {
    rapidjson::Value owner(rapidjson::kObjectType);
    owner.AddMember("name", "Owner", alloc);
    owner.AddMember("typePath", "Owner", alloc);
    owner.AddMember("typeName", "string", alloc);
    owner.AddMember("isTypedef", true, alloc);
    owner.AddMember("staticSize", 32, alloc);
    owner.AddMember("dynamicSize", 0, alloc);

    rapidjson::Value owner_members(rapidjson::kArrayType);
    {
      rapidjson::Value basic_string(rapidjson::kObjectType);
      basic_string.AddMember("name", "", alloc);
      basic_string.AddMember("typePath", "", alloc);
      basic_string.AddMember("typeName",
                             "basic_string<char, std::char_traits<char>, std::allocator<char> >",
                             alloc);
      basic_string.AddMember("isTypedef", false, alloc);
      basic_string.AddMember("staticSize", 32, alloc);
      basic_string.AddMember("dynamicSize", 0, alloc);
      basic_string.AddMember("length", 0, alloc);
      basic_string.AddMember("capacity", 15, alloc);
      basic_string.AddMember("elementStaticSize", 1, alloc);
      owner_members.PushBack(basic_string, alloc);
    }
    owner.AddMember("members", owner_members, alloc);
    members.PushBack(owner, alloc);
  }

  // Member 3: Entries (vector<Contact>)
  {
    rapidjson::Value entries(rapidjson::kObjectType);
    entries.AddMember("name", "Entries", alloc);
    entries.AddMember("typePath", "Entries", alloc);
    entries.AddMember("typeName", "vector<Contact, std::allocator<Contact> >", alloc);
    entries.AddMember("isTypedef", false, alloc);
    entries.AddMember("staticSize", 24, alloc);

    // Each Contact is 96 bytes static + variable dynamic.
    int64_t entries_dynamic = 0;
    rapidjson::Value contact_members(rapidjson::kArrayType);
    for (int i = 0; i < mock_entry_count_; ++i) {
      int64_t first_dyn = 20 + (i % 10);
      int64_t last_dyn = (i % 3 == 0) ? 0 : 18;
      int64_t number_dyn = 12 + (i % 5);
      int64_t contact_dyn = first_dyn + last_dyn + number_dyn;
      entries_dynamic += 96 + contact_dyn;

      rapidjson::Value contact(rapidjson::kObjectType);
      contact.AddMember("name", "", alloc);
      contact.AddMember("typePath", "Contact[]", alloc);
      contact.AddMember("typeName", "Contact", alloc);
      contact.AddMember("isTypedef", false, alloc);
      contact.AddMember("staticSize", 96, alloc);
      contact.AddMember("dynamicSize", contact_dyn, alloc);

      // Contact members: firstName, lastName, number
      rapidjson::Value cmembers(rapidjson::kArrayType);

      auto add_string_member = [&](const char* field_name, int64_t dyn_size) {
        rapidjson::Value str_member(rapidjson::kObjectType);
        str_member.AddMember("name", rapidjson::Value(field_name, alloc), alloc);
        str_member.AddMember("typePath", rapidjson::Value(field_name, alloc), alloc);
        str_member.AddMember("typeName", "string", alloc);
        str_member.AddMember("isTypedef", true, alloc);
        str_member.AddMember("staticSize", 32, alloc);
        str_member.AddMember("dynamicSize", dyn_size, alloc);

        rapidjson::Value inner_members(rapidjson::kArrayType);
        rapidjson::Value inner(rapidjson::kObjectType);
        inner.AddMember("name", "", alloc);
        inner.AddMember("typePath", "", alloc);
        inner.AddMember("typeName",
                        "basic_string<char, std::char_traits<char>, std::allocator<char> >", alloc);
        inner.AddMember("isTypedef", false, alloc);
        inner.AddMember("staticSize", 32, alloc);
        inner.AddMember("dynamicSize", dyn_size, alloc);
        inner.AddMember("length", dyn_size > 0 ? dyn_size : static_cast<int64_t>(8), alloc);
        inner.AddMember("capacity", dyn_size > 0 ? dyn_size : static_cast<int64_t>(15), alloc);
        inner.AddMember("elementStaticSize", 1, alloc);
        inner_members.PushBack(inner, alloc);
        str_member.AddMember("members", inner_members, alloc);
        cmembers.PushBack(str_member, alloc);
      };

      add_string_member("firstName", first_dyn);
      add_string_member("lastName", last_dyn);
      add_string_member("number", number_dyn);

      contact.AddMember("members", cmembers, alloc);
      contact_members.PushBack(contact, alloc);
    }

    entries.AddMember("dynamicSize", entries_dynamic, alloc);
    entries.AddMember("length", mock_entry_count_, alloc);
    entries.AddMember("capacity", static_cast<int64_t>(mock_entry_count_ * 2), alloc);
    entries.AddMember("elementStaticSize", 96, alloc);
    entries.AddMember("members", contact_members, alloc);
    members.PushBack(entries, alloc);

    // Total dynamic size for root.
    root.AddMember("dynamicSize", entries_dynamic, alloc);
  }

  root.AddMember("members", members, alloc);
  doc.PushBack(root, alloc);

  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
  doc.Accept(writer);
  *output = buffer.GetString();
}

// static
void DynamicObjectTraceConnector::FlattenOIJson(const rapidjson::Document& doc,
                                                uint64_t timestamp_ns, md::UPID upid,
                                                DataTable* traces_table, DataTable* stats_table) {
  for (const auto& root : doc.GetArray()) {
    FlattenOIJsonRecursive(root, "", timestamp_ns, upid, traces_table, stats_table);
  }
}

Status DynamicObjectTraceConnector::StopImpl() {
  if (subprocess_ && subprocess_->IsRunning()) {
    LOG(INFO) << "Killing subprocess on stop.";
    subprocess_->Kill();
    subprocess_->Wait();
  }
  subprocess_.reset();

  return trace_status_;
}

}  // namespace stirling
}  // namespace px
