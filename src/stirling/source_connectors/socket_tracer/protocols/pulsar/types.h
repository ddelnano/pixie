#pragma once

#include <cstddef>
#include <string>
#include "src/stirling/source_connectors/socket_tracer/protocols/common/event_parser.h"
#include "src/stirling/source_connectors/socket_tracer/protocols/common/interface.h"

namespace px {
namespace stirling {
namespace protocols {
namespace pulsar {

using stream_id_t = uint16_t;
struct StateWrapper {
  std::vector<std::string> commands;
};
struct Packet : public FrameBase {
  std::string command;
  std::string topic;

  // TODO: Calculate properly
  size_t ByteSize() const override { return command.size() + topic.size(); }
};

struct Record {
  Packet req;

  // Error responses are always sent by server when encounter any error during processing the
  // request. OK responses are only sent by server in the verbose mode.
  // See https://github.com/nats-io/docs/blob/master/nats_protocol/nats-protocol.md#okerr.
  Packet resp;
};

// Required by event parser interface.
struct ProtocolTraits : public BaseProtocolTraits<Record> {
  using frame_type = Packet;
  using record_type = Record;
  using state_type = NoState;
  using key_type = stream_id_t;
};

}  // namespace pulsar
}  // namespace protocols
}  // namespace stirling
}  // namespace px
