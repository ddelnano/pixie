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

#include "src/stirling/obj_tools/address_converter.h"
#include "src/stirling/obj_tools/testdata/cc/test_exe_fixture.h"

#include "src/common/testing/test_environment.h"
#include "src/common/testing/testing.h"
#include "src/stirling/bpf_tools/macros.h"
#include "src/stirling/obj_tools/testdata/containers/address_converter_container.h"
#include "src/stirling/testing/common.h"
#include "src/stirling/utils/proc_path_tools.h"

namespace px {
namespace stirling {
namespace obj_tools {

TEST(ElfAddressConverterTest, VirtualAddrToBinaryAddr) {
  AddressConverterContainer container;
  ASSERT_OK(container.Run());

  int status = -1;
  testing::Timeout t(std::chrono::minutes{1});
  while (status == -1 && !t.TimedOut()) {
    status = container.GetStatus();
    std::this_thread::sleep_for(std::chrono::milliseconds{200});
  }
  EXPECT_EQ(0, status);
}

// This covers the situation discovered in https://github.com/pixie-io/pixie/issues/1630.
// Setting an unlimited stack size ulimit causes the VMAs of a process to be reordered and
// caused stirling to fail to start.
TEST(ElfAddressConverterTest, VirtualAddrToBinaryAddrWithUnlimitedStackUlimit) {
  AddressConverterContainer container;
  ASSERT_OK(container.Run(std::chrono::seconds{5}, {"--ulimit=stack=-1"}));

  int status = -1;
  testing::Timeout t(std::chrono::minutes{1});
  while (status == -1 && !t.TimedOut()) {
    status = container.GetStatus();
    std::this_thread::sleep_for(std::chrono::milliseconds{200});
  }
  EXPECT_EQ(0, status);
}

}  // namespace obj_tools
}  // namespace stirling
}  // namespace px
