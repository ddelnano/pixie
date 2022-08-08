/*
 * Copyright 2022- The Pixie Authors.
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
#include <string>

#include "src/common/base/base.h"
#include "src/stirling/source_connectors/socket_tracer/protocols/common/event_parser.h"  // For FrameBase
#include "src/stirling/utils/utils.h"

namespace px {
namespace stirling {
namespace protocols {
namespace amqp {

enum class AMQPConstant : uint16_t {
  kFrameMethod = 1,
  kFrameHeader = 2,
  kFrameBody = 3,
  kFrameHeartbeat = 8,
  kFrameMinSize = 4096,
  kFrameEnd = 206,
  kReplySuccess = 200,
  kContentTooLarge = 311,
  kNoConsumers = 313,
  kConnectionForced = 320,
  kInvalidPath = 402,
  kAccessRefused = 403,
  kNotFound = 404,
  kResourceLocked = 405,
  kPreconditionFailed = 406,
  kFrameError = 501,
  kSyntaxError = 502,
  kCommandInvalid = 503,
  kChannelError = 504,
  kUnexpectedFrame = 505,
  kResourceError = 506,
  kNotAllowed = 530,
  kNotImplemented = 540,
  kInternalError = 541
};

enum AMQPConnectionMethods : uint8_t {
  kAMQPConnectionStart = 10,
  kAMQPConnectionStartOk = 11,
  kAMQPConnectionSecure = 20,
  kAMQPConnectionSecureOk = 21,
  kAMQPConnectionTune = 30,
  kAMQPConnectionTuneOk = 31,
  kAMQPConnectionOpen = 40,
  kAMQPConnectionOpenOk = 41,
  kAMQPConnectionClose = 50,
  kAMQPConnectionCloseOk = 51
};

enum AMQPChannelMethods : uint8_t {
  kAMQPChannelOpen = 10,
  kAMQPChannelOpenOk = 11,
  kAMQPChannelFlow = 20,
  kAMQPChannelFlowOk = 21,
  kAMQPChannelClose = 40,
  kAMQPChannelCloseOk = 41
};

enum AMQPExchangeMethods : uint8_t {
  kAMQPExchangeDeclare = 10,
  kAMQPExchangeDeclareOk = 11,
  kAMQPExchangeDelete = 20,
  kAMQPExchangeDeleteOk = 21
};

enum AMQPQueueMethods : uint8_t {
  kAMQPQueueDeclare = 10,
  kAMQPQueueDeclareOk = 11,
  kAMQPQueueBind = 20,
  kAMQPQueueBindOk = 21,
  kAMQPQueueUnbind = 50,
  kAMQPQueueUnbindOk = 51,
  kAMQPQueuePurge = 30,
  kAMQPQueuePurgeOk = 31,
  kAMQPQueueDelete = 40,
  kAMQPQueueDeleteOk = 41
};

enum AMQPBasicMethods : uint8_t {
  kAMQPBasicQos = 10,
  kAMQPBasicQosOk = 11,
  kAMQPBasicConsume = 20,
  kAMQPBasicConsumeOk = 21,
  kAMQPBasicCancel = 30,
  kAMQPBasicCancelOk = 31,
  kAMQPBasicPublish = 40,
  kAMQPBasicReturn = 50,
  kAMQPBasicDeliver = 60,
  kAMQPBasicGet = 70,
  kAMQPBasicGetOk = 71,
  kAMQPBasicGetEmpty = 72,
  kAMQPBasicAck = 80,
  kAMQPBasicReject = 90,
  kAMQPBasicRecoverAsync = 100,
  kAMQPBasicRecover = 110,
  kAMQPBasicRecoverOk = 111
};

enum AMQPTxMethods : uint8_t {
  kAMQPTxSelect = 10,
  kAMQPTxSelectOk = 11,
  kAMQPTxCommit = 20,
  kAMQPTxCommitOk = 21,
  kAMQPTxRollback = 30,
  kAMQPTxRollbackOk = 31
};

enum class AMQPClasses : uint8_t {
  kConnection = 10,
  kChannel = 20,
  kExchange = 40,
  kQueue = 50,
  kBasic = 60,
  kTx = 90
};

// Sized based on frame breakdown
// 0      1         3             7                  size+7 size+8
// +------+---------+-------------+  +------------+  +-----------+
// | type | channel |     size    |  |  payload   |  | frame-end |
// +------+---------+-------------+  +------------+  +-----------+
//  octet   short         long         size octets       octet
enum class AMQPFrameSizes : uint8_t {
  kFrameTypeSize = 1,
  kChannelSize = 2,
  kPayloadSize = 4,
  kEndSize = 1
};

enum class AMQPFrameTypes : uint8_t {
  kFrameMethod = 1,
  kFrameHeader = 2,
  kFrameBody = 3,
  kFrameHeartbeat = 8,
};
const uint8_t kFrameEnd = 0xCE;
const uint8_t kMinFrameLength = 8;
constexpr uint8_t kEndByteSize = 1;
const uint8_t kMinFrameWithoutEnd = 7;

// Represents a generic AMQP message.
struct Frame : public FrameBase {
  // Marks end of the frame by hexadecimal value %xCE

  uint8_t frame_type = 0;

  // Communication channel to be used
  uint16_t channel = 0;

  // Defines the length of message upcoming
  uint32_t payload_size = 0;

  // Actual body content to be used
  std::string msg = "";

  // sync value only known after full body parsing
  bool synchronous = false;

  // `consumed` is used to mark if a request packet has been matched to a
  // response in StitchFrames. This is an optimization to efficiently remove all
  // matched packets from the front of the deque.
  bool consumed = false;

  // if full body parsing already done
  bool full_body_parsed = false;

  uint16_t class_id = 0;
  uint16_t method_id = 0;

  size_t ByteSize() const override { return sizeof(Frame) + msg.size(); }

  std::string ToString() const override {
    return absl::Substitute("frame_type=[$0] channel=[$1] payload_size=[$2] msg=[$3]", frame_type,
                            channel, payload_size, msg);
  }
};

struct Record {
  Frame req;
  Frame resp;

  // Debug information.
  std::string px_info = "";
  std::string ToString() const {
    return absl::Substitute("req=[$0] resp=[$1]", req.ToString(), resp.ToString());
  }
};

struct ProtocolTraits : public BaseProtocolTraits<Record> {
  using frame_type = Frame;
  using record_type = Record;
  using state_type = NoState;
};

}  // namespace amqp
}  // namespace protocols
}  // namespace stirling
}  // namespace px
