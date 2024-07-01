
#pragma once

#include <string_view>

#include "src/stirling/source_connectors/socket_tracer/protocols/common/interface.h"
#include "src/stirling/source_connectors/socket_tracer/protocols/pulsar/types.h"

namespace px {
namespace stirling {
namespace protocols {
namespace pulsar {
size_t FindMessageBoundary(std::string_view buf, size_t start_pos);
}  // namespace pulsar
template <>
size_t FindFrameBoundary<pulsar::Packet, NoState>(message_type_t /*type*/, std::string_view buf,
                                                  size_t start_pos, NoState* /*state*/);
template <>
ParseState ParseFrame(message_type_t type, std::string_view* buf, pulsar::Packet* msg,
                      NoState* /*state*/);

}  // namespace protocols
}  // namespace stirling
}  // namespace px
