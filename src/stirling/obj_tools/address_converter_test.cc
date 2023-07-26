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
#include "src/stirling/utils/proc_path_tools.h"
#include <sys/resource.h>

namespace px {
namespace stirling {
namespace obj_tools {

extern "C" {
NO_OPT_ATTR void test_func() { }
}

void FindAddressForSelfFunc() {
  std::filesystem::path self_path = GetSelfPath().ValueOrDie();
  ASSERT_OK_AND_ASSIGN(auto elf_reader, ElfReader::Create(self_path.string()));
  ASSERT_OK_AND_ASSIGN(std::vector<ElfReader::SymbolInfo> syms, elf_reader->ListFuncSymbols("test_func", SymbolMatchType::kSubstr));
  EXPECT_EQ(1, syms.size());
  LOG(WARNING) << absl::Substitute("Found addr=$0 and name=$1", syms[0].address, syms[0].name);

  ASSERT_OK_AND_ASSIGN(auto converter, ElfAddressConverter::Create(elf_reader.get(), getpid()));

  auto symbol_addr =
      converter->VirtualAddrToBinaryAddr(reinterpret_cast<uint64_t>(&test_func));
  LOG(WARNING) << "Found symbol: " << symbol_addr;
  EXPECT_EQ(syms[0].address, symbol_addr);
}

TEST(ElfAddressConverterTest, VirtualAddrToBinaryAddr) {
  FindAddressForSelfFunc();
}

TEST(ElfAddressConverterTest, VirtualAddrToBinaryAddrWithUnlimitedStackUlimit) {
  system::ProcParser parser;
  std::vector<system::ProcParser::ProcessSMaps> pre_fork_map_entries;
  ASSERT_OK(parser.ParseProcPIDMaps(getpid(), &pre_fork_map_entries));
  for (auto& map : pre_fork_map_entries) {
    LOG(WARNING) << "VMA path=" << map.pathname;
  }

  struct rlimit stack_limit = {
    .rlim_cur = RLIM_INFINITY,
    .rlim_max = RLIM_INFINITY,
  };
  auto rv = setrlimit(RLIMIT_STACK, &stack_limit);
  if (rv != 0) {
    LOG(WARNING) << "Failed to set ulimit";
  }
  pid_t child_pid = fork();
  if (child_pid != 0) {
    // Parent process: Wait for results from child.
    sleep(5);
   } else {

      std::vector<system::ProcParser::ProcessSMaps> post_fork_map_entries;
      ASSERT_OK(parser.ParseProcPIDMaps(getpid(), &post_fork_map_entries));
      for (auto& map : post_fork_map_entries) {
        LOG(WARNING) << "VMA path=" << map.pathname;
      }
   }
}

}  // namespace obj_tools
}  // namespace stirling
}  // namespace px
