#pragma once

#include <deque>

#include "src/stirling/source_connectors/socket_tracer/protocols/common/event_parser.h"
#include "src/stirling/source_connectors/socket_tracer/protocols/http2k/http2.h"

namespace px {
namespace stirling {
namespace protocols {

/**
 * Unpacks a single HTTP2 frame from the input string.
 */
template <>
ParseState ParseFrame(message_type_t type, std::string_view* buf, http2k::Frame* frame, NoState*);

template <>
size_t FindFrameBoundary<http2k::Frame>(message_type_t type, std::string_view buf, size_t start_pos, NoState*);

}  // namespace protocols
}  // namespace stirling
}  // namespace pl
