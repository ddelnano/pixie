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

#include "src/stirling/source_connectors/socket_tracer/protocols/sink/parse.h"

#include <initializer_list>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <absl/container/flat_hash_map.h>

#include "src/common/base/base.h"
#include "src/stirling/source_connectors/socket_tracer/protocols/sink/types.h"
#include "src/stirling/utils/binary_decoder.h"

namespace px {
namespace stirling {
namespace protocols {

// template <>
// size_t FindFrameBoundary<sink::Message>(message_type_t /*type*/, std::string_view buf,
//                                          size_t start_pos, NoState* /*state*/) {
//   return 0;
// }
//
// template <>
// ParseState ParseFrame(message_type_t type, std::string_view* buf, sink::Message* msg,
//                       NoState* /*state*/) {
//   buf->remove_prefix(buf->size());
//   return Status::OK();
//   // return sink::ParseMessage(type, buf, msg);
// }
//
// }  //
}  // namespace protocols
}  // namespace stirling
}  // namespace px