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

#include "src/stirling/source_connectors/socket_tracer/protocols/pulsar/stitcher.h"
#include "src/common/base/macros.h"

namespace px {
namespace stirling {
namespace protocols {

template <>
RecordsWithErrorCount<pulsar::Record> StitchFrames(std::deque<pulsar::Packet>* req_msgs,
                                                   std::deque<pulsar::Packet>* resp_msgs,
                                                   NoState* /* state */) {
  std::vector<pulsar::Record> records;
  auto request_messages = req_msgs->begin();
  auto response_messages = resp_msgs->begin();

  PX_UNUSED(request_messages);
  PX_UNUSED(response_messages);

  while (request_messages != req_msgs->end() && response_messages != resp_msgs->end()) {
    records.push_back({std::move(*request_messages), std::move(*response_messages)});
    request_messages++;
    response_messages++;
  }
  req_msgs->clear();
  resp_msgs->clear();
  return {std::move(records), 0};
}
}  // namespace protocols
}  // namespace stirling
}  // namespace px
