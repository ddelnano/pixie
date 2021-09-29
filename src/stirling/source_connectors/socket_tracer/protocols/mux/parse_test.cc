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

#include "src/stirling/source_connectors/socket_tracer/protocols/mux/parse.h"

#include <string>
#include <utility>

#include "src/common/testing/testing.h"

namespace px {
namespace stirling {
namespace protocols {

constexpr std::string_view tinitCheck = ConstStringView("tinit check");

//   00000000  00 00 00 0f 7f 00 00 01  74 69 6e 69 74 20 63 68  |........tinit ch|
//   00000010  65 63 6b                                          |eck|

// Size header: 15 Type: 127 Tag: 1 Why: tinit check NumCtx: 0 Ctx: map[] Dest:  Dtabs: map[] Payload length: 0
constexpr uint8_t muxTinitFrame[] = {
    // mux header length (15 bytes)
    0x00, 0x00, 0x00, 0x0f,
    // type and tag
    0x7f, 0x00, 0x00, 0x01,
    // why
    0x74, 0x69, 0x6e, 0x69,
    0x74, 0x20, 0x63, 0x68,
    0x65, 0x63, 0x6b,
};

constexpr uint8_t muxNeedMoreData[] = {
    // mux header length (15 bytes)
    0x00, 0x00, 0x00, 0x0f,
    // type and tag
    0x7f, 0x00, 0x00, 0x01,
};

constexpr uint8_t muxTdispatchFrame[] = {
//           |   mux header length | type |       tag      | # context |
/* 0042 */   0x00, 0x00, 0x00, 0xc8, 0x02, 0x80, 0x00, 0x0f, 0x00, 0x03, 0x00, 0x28, 0x63, 0x6f,
/* 0050 */   0x6d, 0x2e, 0x74, 0x77, 0x69, 0x74, 0x74, 0x65, 0x72, 0x2e, 0x66, 0x69, 0x6e, 0x61, 0x67, 0x6c,
/* 0060 */   0x65, 0x2e, 0x74, 0x72, 0x61, 0x63, 0x69, 0x6e, 0x67, 0x2e, 0x54, 0x72, 0x61, 0x63, 0x65, 0x43,
/* 0070 */   0x6f, 0x6e, 0x74, 0x65, 0x78, 0x74, 0x00, 0x20, 0x42, 0xea, 0x40, 0xa4, 0xcb, 0x74, 0x9e, 0xd6,
/* 0080 */   0x42, 0xea, 0x40, 0xa4, 0xcb, 0x74, 0x9e, 0xd6, 0x42, 0xea, 0x40, 0xa4, 0xcb, 0x74, 0x9e, 0xd6,
/* 0090 */   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1b, 0x63, 0x6f, 0x6d, 0x2e, 0x74, 0x77,
/* 00a0 */   0x69, 0x74, 0x74, 0x65, 0x72, 0x2e, 0x66, 0x69, 0x6e, 0x61, 0x67, 0x6c, 0x65, 0x2e, 0x52, 0x65,
/* 00b0 */   0x74, 0x72, 0x69, 0x65, 0x73, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1c, 0x63, 0x6f, 0x6d,
/* 00c0 */   0x2e, 0x74, 0x77, 0x69, 0x74, 0x74, 0x65, 0x72, 0x2e, 0x66, 0x69, 0x6e, 0x61, 0x67, 0x6c, 0x65,
/* 00d0 */   0x2e, 0x44, 0x65, 0x61, 0x64, 0x6c, 0x69, 0x6e, 0x65, 0x00, 0x10, 0x16, 0x95, 0x3b, 0x99, 0xd1,
/* 00e0 */   0x7d, 0x7e, 0xc0, 0x7f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00,
//
// These lines are repeated to avoid kNeedsMoreData
/* 00e0 */   0x7d, 0x7e, 0xc0, 0x7f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00,
/* 00e0 */   0x7d, 0x7e, 0xc0, 0x7f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00,
};



class MuxParserTest : public ::testing::Test {};

TEST_F(MuxParserTest, ParseFrameWhenNeedsMoreData) {
  auto frame_view = CreateStringView<char>(CharArrayStringView<uint8_t>(muxNeedMoreData));

  mux::Frame frame;
  ParseState state = ParseFrame(message_type_t::kRequest, &frame_view, &frame);

  ASSERT_EQ(frame.header_length, 15);
  ASSERT_EQ(state, ParseState::kNeedsMoreData);
}

TEST_F(MuxParserTest, ParseFrameCanITinit) {
  auto frame_view = CreateStringView<char>(CharArrayStringView<uint8_t>(muxTinitFrame));

  mux::Frame frame;
  ParseState state = ParseFrame(message_type_t::kRequest, &frame_view, &frame);

  ASSERT_EQ(state, ParseState::kSuccess);
  ASSERT_EQ(frame.header_length, 15);

  ASSERT_EQ(frame.tag, 1);
  ASSERT_EQ(frame.type, mux::RerrOld);

  ASSERT_EQ(frame.why, tinitCheck);
}

TEST_F(MuxParserTest, ParseFrameTdispatch) {
  auto frame_view = CreateStringView<char>(CharArrayStringView<uint8_t>(muxTdispatchFrame));

  mux::Frame frame;
  ParseState state = ParseFrame(message_type_t::kRequest, &frame_view, &frame);

  // Verify that tags 24 bit wide are properly constructed
  ASSERT_EQ(frame.tag, 0x80000f);
  ASSERT_EQ(frame.type, mux::Tdispatch);
  ASSERT_EQ(state, ParseState::kSuccess);

  ASSERT_EQ(frame.context.size(), 3);
  std::map<std::string, std::string> traceCtx = {
    {"span id", "4821737427585769174"},
    {"parent id", "4821737427585769174"},
    {"trace id", "4821737427585769174"},
    {"flags", "0"},
  };
  std::map<std::string, std::string> deadline = {
    {"timestamp", "1627272372195000"},
    {"deadline", "9223372036854775"},
  };
  std::map<std::string, std::string> retries = {
    {"length", "4"},
  };
  ASSERT_EQ(frame.context["com.twitter.finagle.tracing.TraceContext"], traceCtx);
  ASSERT_EQ(frame.context["com.twitter.finagle.Deadline"], deadline);
  ASSERT_EQ(frame.context["com.twitter.finagle.Retries"], retries);
}

namespace mux {

}  // namespace mux
}  // namespace protocols
}  // namespace stirling
}  // namespace px
