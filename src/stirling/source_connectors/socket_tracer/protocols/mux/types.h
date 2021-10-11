#pragma once

#include <magic_enum.hpp>
#include "src/stirling/source_connectors/socket_tracer/protocols/common/event_parser.h"
#include "src/stirling/utils/utils.h"

namespace px {
namespace stirling {
namespace protocols {
namespace mux {

enum class Type : int8_t {
    Treq = 1,
    Rreq = -1,
    Tdispatch = 2,
    Rdispatch = -2,

    // control messages
    Tdrain = 64,
    Rdrain = -64,
    Tping  = 65,
    Rping  = -65,

    Tdiscarded = 66,
    Rdiscarded = -66,

    Tlease = 67,

    Tinit = 68,
    Rinit = -68,

    Rerr = -128,

    // only used to preserve backwards compatibility
    TdiscardedOld = -62,
    RerrOld       = 127,
};

inline bool IsMuxType(int8_t t) {
  std::optional<Type> mux_type = magic_enum::enum_cast<Type>(t);
  return mux_type.has_value();
}

inline Type GetMatchingRespType(Type req_type) {
    switch (req_type) {
        case Type::RerrOld:
            return Type::RerrOld;
        case Type::Rerr:
            return Type::Rerr;
        case Type::Tinit:
            return Type::Rinit;
        case Type::Tping:
            return Type::Rping;
        case Type::Treq:
            return Type::Rreq;
        case Type::Tdrain:
            return Type::Rdrain;
        case Type::Tdispatch:
            return Type::Rdispatch;
        case Type::TdiscardedOld:
        case Type::Tdiscarded:
            return Type::Rdiscarded;
        default:
            LOG(DFATAL) << absl::Substitute("Unexpected request type $0", magic_enum::enum_name(req_type));
            return Type::Tlease;
    }
}

/**
 * The mux protocol is explained in more detail in the finagle source code
 * here (https://github.com/twitter/finagle/blob/release/finagle-mux/src/main/scala/com/twitter/finagle/mux/package.scala)
 *
 * Mux messages can take on a few different wire formats. Each type
 * is described below. All fields are big endian.
 *
 * Regular message
 * ----------------------------------------------
 * | uint32 header size | int8 type | int24 tag |
 * ----------------------------------------------
 * |                 Payload                    |
 * ----------------------------------------------
 * 
 * Rinit message
 * ----------------------------------------------
 * | uint32 header size | int8 type | int24 tag |
 * ----------------------------------------------
 * |                   Why                      |
 * ----------------------------------------------
 *
 * Rdispatch / Tdispatch (Tdispatch does not have reply status)
 * ----------------------------------------------
 * | uint32 header size | int8 type | int24 tag |
 * ----------------------------------------------
 * |            uint8 reply status              |
 * ----------------------------------------------
 * | uint16 # context | uint16 ctx key length   |
 * ----------------------------------------------
 * | ctx key          | uint16 ctx value length |
 * ----------------------------------------------
 * | ctx value        | uint16 ctx value length |
 * ----------------------------------------------
 * | uint16 destination length | uint16 # dtabs |
 * ----------------------------------------------
 * | uint16 source len |         source         |
 * ----------------------------------------------
 * | uint16 dest len   |       destination      |
 * ----------------------------------------------
 */
struct Frame : public FrameBase {
  uint32_t length;
  int8_t type;
  uint32_t tag;
  std::string why;
  std::map<std::string, std::map<std::string, std::string>> context;

  size_t ByteSize() const override { return length; }

  // TODO: Include printing the context, dtabs and other fields
  std::string ToString() const override {
    return absl::Substitute("Mux message [len=$0 type=$1 tag=$2 # context: TBD dtabs: TBD]", length, type, tag);
  }

};

struct Record {
    Frame req;
    Frame resp;
};

struct ProtocolTraits {
  using frame_type = Frame;
  using record_type = Record;
  // TODO: mux does have state but assume no state for now
  using state_type = NoState;
};

}
}
}
}
