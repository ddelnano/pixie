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

#include <chrono>
#include <memory>
#include <string>

#include "src/common/base/base.h"
#include "src/common/exec/subprocess.h"
#include "src/stirling/core/source_connector.h"
#include "src/stirling/source_connectors/dynamic_tracer/dynamic_tracing/ir/logicalpb/logical.pb.h"

namespace px {
namespace stirling {

/**
 * DynamicObjectTraceConnector is a source connector for object introspection.
 * It forks a subprocess to read from a target process's stdout and monitors
 * its lifecycle, relaunching as needed until the required count of successful
 * completions is reached or TTL is exceeded.
 */
class DynamicObjectTraceConnector : public SourceConnector {
 public:
  static constexpr auto kSamplingPeriod = std::chrono::milliseconds{100};
  static constexpr auto kPushPeriod = std::chrono::milliseconds{1000};

  ~DynamicObjectTraceConnector() override = default;

  static StatusOr<std::unique_ptr<SourceConnector>> Create(
      std::string_view name, dynamic_tracing::ir::logical::TracepointDeployment* program);

 protected:
  DynamicObjectTraceConnector(std::string_view name, ArrayView<DataTableSchema> table_schemas,
                              uint32_t target_pid, std::chrono::nanoseconds ttl,
                              int32_t required_count);

  Status InitImpl() override;

  void TransferDataImpl(ConnectorContext* ctx) override;

  Status StopImpl() override;

 private:
  // Start the subprocess to tail the target process's stdout.
  Status StartSubprocess();

  // Check if the target process is still alive.
  bool IsTargetProcessAlive() const;

  // Check if TTL has been exceeded.
  bool IsTTLExceeded() const;

  // The PID of the target process to monitor.
  uint32_t target_pid_;

  // The time-to-live for this trace.
  std::chrono::nanoseconds ttl_;

  // The number of successful subprocess completions required.
  int32_t required_count_;

  // The number of successful subprocess completions so far.
  int32_t successful_completions_ = 0;

  // The time when this connector was initialized.
  std::chrono::steady_clock::time_point start_time_;

  // The subprocess that tails the target process's stdout.
  std::unique_ptr<SubProcess> subprocess_;

  // Whether the trace is complete (either succeeded or failed).
  bool trace_complete_ = false;

  // The final status of the trace.
  Status trace_status_ = Status::OK();
};

}  // namespace stirling
}  // namespace px
