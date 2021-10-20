#include "src/stirling/source_connectors/socket_tracer/protocols/mux/parse.h"
#include "src/stirling/utils/binary_decoder.h"

namespace px {
namespace stirling {
namespace protocols {
namespace mux {

ParseState ParseFullFrame(BinaryDecoder* decoder, Frame* frame) {

  // TODO(oazizi/ddelnano): Simplify this logic when the binary decoder supports reading 24 bit fields
  PL_ASSIGN_OR(uint16_t tag_first, decoder->ExtractInt<uint16_t>(), return ParseState::kInvalid);
  PL_ASSIGN_OR(uint8_t tag_last, decoder->ExtractInt<uint8_t>(), return ParseState::kInvalid);
  frame->tag = (tag_first << 8) | tag_last;

  Type frame_type = static_cast<Type>(frame->type);

  if (frame_type == Type::kRerrOld || frame_type == Type::kRerr) {
    PL_ASSIGN_OR(std::string_view why, decoder->ExtractString(frame->MuxBodyLength()), return ParseState::kInvalid);
    frame->why = std::string(why);
    return ParseState::kSuccess;
  }

  if (frame_type == Type::kRinit || frame_type == Type::kTinit) {
    // TODO(ddelnano): Add support for reading Tinit and Rinit compression, tls and other parameters
    return ParseState::kSuccess;
  }

  if (frame_type == Type::kRdispatch) {
      PL_ASSIGN_OR(frame->reply_status, decoder->ExtractInt<uint8_t>(), return ParseState::kInvalid);
  }

  PL_ASSIGN_OR(int16_t num_ctx, decoder->ExtractInt<int16_t>(), return ParseState::kInvalid);
  std::map<std::string, std::map<std::string, std::string>> context;

  for (int i = 0; i < num_ctx; i++) {
    PL_ASSIGN_OR(size_t ctx_key_len, decoder->ExtractInt<int16_t>(), return ParseState::kInvalid);

    PL_ASSIGN_OR(std::string_view ctx_key, decoder->ExtractString(ctx_key_len), return ParseState::kInvalid);

    PL_ASSIGN_OR(size_t ctx_value_len, decoder->ExtractInt<int16_t>(), return ParseState::kInvalid);

    std::map<std::string, std::string> unpacked_value;
    if (ctx_key == "com.twitter.finagle.Deadline") {

      PL_ASSIGN_OR(int64_t timestamp, decoder->ExtractInt<int64_t>(), return ParseState::kInvalid);
      PL_ASSIGN_OR(int64_t deadline, decoder->ExtractInt<int64_t>(), return ParseState::kInvalid);

      unpacked_value["timestamp"] = std::to_string(timestamp / 1000);
      unpacked_value["deadline"] = std::to_string(deadline / 1000);

    } else if (ctx_key == "com.twitter.finagle.tracing.TraceContext") {

      PL_ASSIGN_OR(int64_t span_id, decoder->ExtractInt<int64_t>(), return ParseState::kInvalid);
      PL_ASSIGN_OR(int64_t parent_id, decoder->ExtractInt<int64_t>(), return ParseState::kInvalid);
      PL_ASSIGN_OR(int64_t trace_id, decoder->ExtractInt<int64_t>(), return ParseState::kInvalid);
      PL_ASSIGN_OR(int64_t flags, decoder->ExtractInt<int64_t>(), return ParseState::kInvalid);

      unpacked_value["span id"] = std::to_string(span_id);
      unpacked_value["parent id"] = std::to_string(parent_id);
      unpacked_value["trace id"] = std::to_string(trace_id);
      unpacked_value["flags"] = std::to_string(flags);

    } else if (ctx_key == "com.twitter.finagle.thrift.ClientIdContext") {

      PL_ASSIGN_OR(std::string_view ctx_value, decoder->ExtractString(ctx_value_len), return ParseState::kInvalid);
      unpacked_value["name"] = std::string(ctx_value);

    } else {

      PL_ASSIGN_OR(std::string_view ctx_value, decoder->ExtractString(ctx_value_len), return ParseState::kInvalid);
      unpacked_value["length"] = std::to_string(ctx_value.length());

    }

    context.insert({std::string(ctx_key), unpacked_value});
  }

  frame->context = std::move(context);

  // TODO(ddelnano): Add dest and dtab parsing here
  return ParseState::kSuccess;
}

}


template <>
ParseState ParseFrame(message_type_t, std::string_view* buf, mux::Frame* frame, NoState*) {

  BinaryDecoder decoder(*buf);

  PL_ASSIGN_OR(frame->length, decoder.ExtractInt<int32_t>(), return ParseState::kInvalid);
  if (frame->length > buf->length()) {
    return ParseState::kNeedsMoreData;
  }

  PL_ASSIGN_OR(frame->type, decoder.ExtractInt<int8_t>(), return ParseState::kInvalid);
  if (!mux::IsMuxType(frame->type)) {
    return ParseState::kInvalid;
  }

  ParseState parse_state = mux::ParseFullFrame(&decoder, frame);
  if (parse_state == ParseState::kSuccess) {
    buf->remove_prefix(frame->length);
  }
  return parse_state;
}

template <>
size_t FindFrameBoundary<mux::Frame>(message_type_t, std::string_view,
                                      size_t, NoState*) {
  // Not implemented.
  return std::string::npos;
}

}
}
}
