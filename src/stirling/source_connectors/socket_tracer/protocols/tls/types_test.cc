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

#include "src/stirling/source_connectors/socket_tracer/protocols/tls/types.h"

#include "src/common/testing/testing.h"

namespace px {
namespace stirling {
namespace protocols {
namespace tls {

TEST(TypeTest, TestTLSVersionToString) {
  /* EXPECT_EQ(TLSVersionToString(TLSVersion::kTLS1_0), "TLS1.0"); */
  /* EXPECT_EQ(TLSVersionToString(TLSVersion::kTLS1_1), "TLS1.1"); */
  /* EXPECT_EQ(TLSVersionToString(TLSVersion::kTLS1_2), "TLS1.2"); */
  /* EXPECT_EQ(TLSVersionToString(TLSVersion::kTLS1_3), "TLS1.3"); */
  /* EXPECT_EQ(TLSVersionToString(TLSVersion::kUnknown), "Unknown"); */
}

}  // namespace tls
}  // namespace protocols
}  // namespace stirling
}  // namespace px
