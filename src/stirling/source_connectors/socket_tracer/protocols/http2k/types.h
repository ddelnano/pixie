#pragma once

#include <nghttp2/nghttp2_frame.h>

#include <map>
#include <string>

#include "src/common/base/base.h"
#include "src/stirling/utils/parse_state.h"
#include "src/stirling/common/utils.h"
#include "src/stirling/source_connectors/socket_tracer/protocols/common/event_parser.h"  // For FrameBase

namespace px {
namespace stirling {
namespace protocols {
namespace http2k {

constexpr size_t kGRPCMessageHeaderSizeInBytes = 5;

namespace headers {

constexpr char kContentType[] = "content-type";
constexpr char kMethod[] = ":method";
constexpr char kPath[] = ":path";

constexpr char kContentTypeGRPC[] = "application/grpc";

}  // namespace headers

using u8string = std::basic_string<uint8_t>;

// Note that NVMap keys (HTTP2 header field names) are assumed to be lowercase to match spec:
//
// From https://http2.github.io/http2-spec/#HttpHeaders:
// ... header field names MUST be converted to lowercase prior to their encoding in HTTP/2.
// A request or response containing uppercase header field names MUST be treated as malformed.
class NVMap : public std::multimap<std::string, std::string> {
 public:
  using std::multimap<std::string, std::string>::multimap;

  std::string ValueByKey(const std::string& key, const std::string& default_value = "") const {
    const auto iter = find(key);
    if (iter != end()) {
      return iter->second;
    }
    return default_value;
  }

  std::string DebugString() const { return absl::StrJoin(*this, ", ", absl::PairFormatter(":")); }
};

/**
 * @brief A wrapper around  nghttp2_frame. nghttp2_frame misses some fields, for example, it has no
 * data body field in nghttp2_data. The payload is a name meant to be generic enough so that it can
 * be used to store such fields for different message types.
 */
struct Frame : public FrameBase {
  // TODO(yzhao): Consider use std::unique_ptr<nghttp2_frame> to avoid copy.
  nghttp2_frame frame;
  u8string u8payload;

  // If true, means this frame is processed and can be destroyed.
  mutable bool consumed = false;

  // Only meaningful for HEADERS frame, indicates if a frame syncing error is detected.
  px::stirling::ParseState frame_sync_state = px::stirling::ParseState::kUnknown;
  // Only meaningful for HEADERS frame, indicates if a header block is already processed.
  px::stirling::ParseState headers_parse_state = px::stirling::ParseState::kUnknown;
  NVMap headers;

  // frame{} zero initialize the member, which is needed to make sure default value is sensible.
  Frame() : frame{} {};
  ~Frame() {
    if (frame.hd.type == NGHTTP2_HEADERS) {
      // We do not use NGHTT2's storage constructs for headers.
      // This check forbids this.
      DCHECK(frame.headers.nva == nullptr);
      DCHECK_EQ(frame.headers.nvlen, 0u);
    }
  }
  size_t ByteSize() const override {
    return sizeof(Frame) + u8payload.size() + CountStringMapSize(headers);
  }
};

struct HTTP2Message {
  // TODO(yzhao): We keep this field for easier testing. Update tests to not rely on input invalid
  // data.
  ParseState parse_state = ParseState::kUnknown;
  ParseState headers_parse_state = ParseState::kUnknown;
  message_type_t type = message_type_t::kUnknown;
  uint64_t timestamp_ns = 0;

  NVMap headers;
  std::string message;
  std::vector<const Frame*> frames;

  void MarkFramesConsumed() const {
    for (const auto* f : frames) {
      f->consumed = true;
    }
  }

  bool HasGRPCContentType() const {
    return absl::StrContains(headers.ValueByKey(headers::kContentType), headers::kContentTypeGRPC);
  }
};

}  // namespace http2k
}  // namespace protocols
}  // namespace stirling
}  // namespace px
