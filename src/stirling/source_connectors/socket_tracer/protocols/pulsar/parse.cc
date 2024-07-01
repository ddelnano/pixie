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

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <ostream>
#include <vector>

#include <absl/container/flat_hash_map.h>

#include "src/common/base/base.h"
#include "src/stirling/source_connectors/socket_tracer/protocols/common/interface.h"
#include "src/stirling/source_connectors/socket_tracer/protocols/pulsar/ir/PulsarApi.pb.h"
#include "src/stirling/source_connectors/socket_tracer/protocols/pulsar/parse.h"
#include "src/stirling/source_connectors/socket_tracer/protocols/pulsar/types.h"
#include "src/stirling/utils/binary_decoder.h"

namespace px {
namespace stirling {
namespace protocols {
namespace pulsar {

const int kBaseCommandIdentifier = 0x08;
const int kComandTypeMinimumValue = ::pulsar::proto::BaseCommand_Type_Type_MIN;
const int kComandTypeMaximumValue = ::pulsar::proto::BaseCommand_Type_Type_MAX;

bool isValidBaseCommand(char first, char second) {
  return first == kBaseCommandIdentifier && second >= kComandTypeMinimumValue &&
         second <= kComandTypeMaximumValue;
}

size_t FindMessageBoundary(std::string_view buf, size_t size) {
  for (size_t i = 0; i < size - 1; i++) {
    if (isValidBaseCommand(buf.at(i), buf.at(i + 1)) && i >= 8) return i - 8;
  }
  return std::string_view::npos;
}

ParseState ParseMessage(message_type_t, std::string_view* buf, Packet* packet) {
  BinaryDecoder decoder(*buf);

  std::cout << "buf->length(): " << buf->length() << std::endl;
  PX_ASSIGN_OR(uint32_t payload_size, decoder.ExtractBEInt<uint32_t>(),
               return ParseState::kInvalid);
  PX_ASSIGN_OR(uint32_t current_message_size, decoder.ExtractBEInt<uint32_t>(),
               return ParseState::kInvalid);
  PX_UNUSED(current_message_size);

  if (decoder.eof() || decoder.BufSize() < 2) return ParseState::kNeedsMoreData;

  // Make sure we are dealing with a BaseCommand
  if (!isValidBaseCommand(decoder.Buf().front(), decoder.Buf().at(1))) return ParseState::kInvalid;

  std::cout << "IS VALID COMMAND" << std::endl;
  if (buf->length() - sizeof(int32_t) < payload_size) return ParseState::kNeedsMoreData;

  *buf = decoder.Buf();

  ::pulsar::proto::BaseCommand base_command;
  assert(base_command.ParseFromArray(buf->data(), current_message_size));

  packet->command = base_command.Type_Name(base_command.type());

  std::cout << "YAY " << packet->command << std::endl;

  buf->remove_prefix(payload_size - 4);

  return ParseState::kSuccess;
}

}  // namespace pulsar

template <>
size_t FindFrameBoundary<pulsar::Packet, NoState>(message_type_t /*type*/, std::string_view buf,
                                                  size_t start_pos, NoState* /*state*/) {
  return pulsar::FindMessageBoundary(buf, start_pos);
}

template <>
ParseState ParseFrame(message_type_t type, std::string_view* buf, pulsar::Packet* msg,
                      NoState* /*state*/) {
  return pulsar::ParseMessage(type, buf, msg);
}

}  // namespace protocols
}  // namespace stirling
}  // namespace px
