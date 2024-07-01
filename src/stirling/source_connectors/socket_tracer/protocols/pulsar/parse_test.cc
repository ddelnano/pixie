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

#include <absl/container/flat_hash_map.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <deque>

#include "src/stirling/source_connectors/socket_tracer/protocols/common/event_parser.h"
#include "src/stirling/source_connectors/socket_tracer/protocols/common/interface.h"
#include "src/stirling/source_connectors/socket_tracer/protocols/pulsar/parse.h"
#include "src/stirling/source_connectors/socket_tracer/protocols/pulsar/test_data.h"
#include "src/stirling/source_connectors/socket_tracer/protocols/pulsar/types.h"

namespace px {
namespace stirling {
namespace protocols {
namespace pulsar {

using ::testing::ElementsAre;
using ::testing::ElementsAreArray;

class PulsarParserTest : public ::testing::Test {};

TEST_F(PulsarParserTest, ParseConnect) {
  NoState state{};

  absl::flat_hash_map<int64_t, std::deque<Packet>> parsed_messages;
  auto result = ParseFramesLoop(message_type_t::kRequest, kConnectData, &parsed_messages, &state);
  EXPECT_EQ(ParseState::kSuccess, result.state);

  EXPECT_EQ(parsed_messages.size(), 1);
  EXPECT_EQ(parsed_messages.begin()->second.size(), 1);
  EXPECT_EQ(parsed_messages.begin()->second.begin()->command, "CONNECT");
}

// TEST_F(PulsarParserTest, ParseConnectCrooked) {
//   NoState state{};

//   absl::flat_hash_map<int64_t, std::deque<Packet>> parsed_messages;
//   auto result = ParseFramesLoop(message_type_t::kRequest, kConnectBeginsWith2ExtraBytes,
//                                 &parsed_messages, &state);
//   EXPECT_EQ(ParseState::kSuccess, result.state);

//   EXPECT_EQ(parsed_messages.size(), 1);
//   EXPECT_EQ(parsed_messages.begin()->second.size(), 1);
//   EXPECT_EQ(parsed_messages.begin()->second.begin()->command, "CONNECT");
// }

TEST_F(PulsarParserTest, ParseSend) {
  NoState state{};
  absl::flat_hash_map<int64_t, std::deque<Packet>> parsed_messages;
  auto result =
      ParseFramesLoop(message_type_t::kRequest, kProduceMessage, &parsed_messages, &state);
  EXPECT_EQ(ParseState::kSuccess, result.state);

  EXPECT_EQ(parsed_messages.begin()->second.size(), 1);
  EXPECT_EQ(parsed_messages.begin()->second.begin()->command, "SEND");
}

TEST_F(PulsarParserTest, FindPosition) {
  ASSERT_EQ(0, FindMessageBoundary(kConnectData, kConnectData.size()));
  ASSERT_EQ(2, FindMessageBoundary(kConnectBeginsWith2ExtraBytes, kConnectData.size()));
}
}  // namespace pulsar
}  // namespace protocols
}  // namespace stirling
}  // namespace px
