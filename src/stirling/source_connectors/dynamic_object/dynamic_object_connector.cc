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
#include <utility>

#include "src/common/base/base.h"
#include "src/common/fs/fs_wrapper.h"
#include "src/stirling/core/canonical_types.h"

namespace px {
namespace stirling {

namespace {

// Minimal table schema with just a time column for now - will be expanded later.
constexpr DataElement kElements[] = {
    canonical_data_elements::kTime,
};
constexpr auto kTable = DataTableSchema("object_trace", "Object trace data", kElements);

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
      name, ArrayView<DataTableSchema>(&kTable, 1), target_pid, ttl, required_count));

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

  // Read any available output from the subprocess.
  std::string output;
  auto status = subprocess_->Stdout(&output);
  if (!status.ok()) {
    LOG(WARNING) << "Failed to read subprocess stdout: " << status.ToString();
  } else if (!output.empty()) {
    LOG(INFO) << "Subprocess output (" << output.size() << " bytes): " << output;
  }

  // Check if the subprocess is still running.
  if (!is_running) {
    int exit_status = subprocess_->Wait(/*close_pipe=*/false);

    // Read any remaining output.
    status = subprocess_->Stdout(&output);
    if (status.ok() && !output.empty()) {
      LOG(INFO) << "Subprocess final output: " << output;
    }

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
