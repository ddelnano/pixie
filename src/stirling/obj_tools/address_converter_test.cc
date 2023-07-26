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

namespace px {
namespace stirling {
namespace obj_tools {

const TestExeFixture kTestExeFixture;

class ElfAddressConverterTest : public ::testing::Test {
 protected:
  void SetUp() override { ASSERT_OK(container_.Run()); }

  TestExeContainer container_;
};

TEST(ElfAddressConverterTest, VirtualAddrToBinaryAddr) {
  ASSERT_OK_AND_ASSIGN(auto elf_reader, ElfReader::Create(kTestExeFixture.Path()));
  ASSERT_OK_AND_ASSIGN(std::vector<ElfReader::SymbolInfo> syms, elf_reader->ListFuncSymbols("CanYouFindThis", SymbolMatchType::kSubstr));
  EXPECT_EQ(1, syms.size());
}

}  // namespace obj_tools
}  // namespace stirling
}  // namespace px
