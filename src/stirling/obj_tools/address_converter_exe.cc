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

// This executable is only for testing purposes.
// We use it to see if we can find the function symbols and debug information.

#include <unistd.h>
#include <iostream>
#include "src/stirling/obj_tools/address_converter.h"

#include "src/stirling/utils/proc_path_tools.h"

#if defined(__clang__)
#define NO_OPT_ATTR __attribute__((noinline, optnone))
#elif defined(__GNUC__) || defined(__GNUG__)
#define NO_OPT_ATTR __attribute__((noinline, optimize("O0")))
#endif

// Using extern C to avoid name mangling (which just keeps the test a bit more readable).
extern "C" {
NO_OPT_ATTR void TestFunc() { }
NO_OPT_ATTR void TestFunc2() { }

}  // extern "C"

using px::stirling::obj_tools::ElfReader;
using px::stirling::obj_tools::ElfAddressConverter;
using px::stirling::obj_tools::SymbolMatchType;
using px::system::ProcParser;
using px::stirling::GetSelfPath;

int main() {
  ProcParser parser;
  std::vector<ProcParser::ProcessSMaps> pre_fork_map_entries;
  auto s = parser.ParseProcPIDMaps(getpid(), &pre_fork_map_entries);
  if (!s.ok()) {
     LOG(WARNING) << "Failed to get proc pid maps " << s.msg();
     return -1;
  }
  for (auto& map : pre_fork_map_entries) {
    LOG(WARNING) << "VMA path=" << map.pathname;
  }

  std::filesystem::path self_path = GetSelfPath().ValueOrDie();
  PX_ASSIGN_OR(auto elf_reader, ElfReader::Create(self_path.string()), return -1);
  PX_ASSIGN_OR(std::vector<ElfReader::SymbolInfo> syms, elf_reader->ListFuncSymbols("TestFunc", SymbolMatchType::kSubstr));
  LOG(WARNING) << absl::Substitute("Found addr=$0 and name=$1", syms[0].address, syms[0].name);

  PX_ASSIGN_OR(auto converter, ElfAddressConverter::Create(elf_reader.get(), getpid()), return -1);

  auto symbol_addr =
      converter->VirtualAddrToBinaryAddr(reinterpret_cast<uint64_t>(&TestFunc));

  if (symbol_addr != syms[0].address) {
    LOG(ERROR) << absl::Substitute("Expected ElfAddressConverter address=$0 to match binary address=$1", symbol_addr, syms[0].address);
    return -1;
  }
  return 0;
}
