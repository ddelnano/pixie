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

#include <dlfcn.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cerrno>
#include <fcntl.h>
#include <sched.h>
#include <linux/sched.h>
#include <sys/syscall.h>
#include <memory>
#include <sys/mount.h>

#include "src/common/testing/testing.h"
#include "src/stirling/obj_tools/elf_reader.h"
#include "src/stirling/obj_tools/raw_fptr_manager.h"

namespace px {
namespace stirling {
namespace obj_tools {

using ::px::stirling::obj_tools::ElfReader;

template <class T>
StatusOr<T*> DLSym(void* handle, const std::string& name) {
  T* fptr = reinterpret_cast<T*>(dlsym(handle, name.c_str()));

  const char* dlsym_error = dlerror();
  if (dlsym_error) {
    return error::NotFound("Cannot load symbol: $0", dlsym_error);
  }

  return fptr;
}

static int child_func(void*) {
    /* auto rv = unshare(CLONE_NEWNS); */
    /* if (rv != 0) return -10; */

    auto fd = open("/usr/lib/libdependency.so", O_CREAT);
    if (fd == -1) { 
      /* FAIL() << "open() call failed"; */
      return -1;
    }
    auto out = mount("/vagrant/bazel-out/host/bin/src/stirling/obj_tools/testdata/c/libdependency.so", "/usr/lib/libdependency.so", NULL, MS_SLAVE, NULL);
    if (out != 0) {
      /* FAIL() << "mount() call failed"; */
      return -2;
    }

    printf("this is working");
    sleep(30);
    return 0;
}

// This test does not use RawFptrManager, but shows the behavior of trying to call a function in a
// shared object's dynsym table vs a function in the symtab table.
TEST(Basic, DlopenBySymbolTypes) {
  std::filesystem::path so_path =
      px::testing::BazelRunfilePath("src/stirling/obj_tools/testdata/c/library.so");

  LOG(WARNING) << absl::Substitute("Bazel runfile path: $0", so_path.string());
  void* handle = dlopen(so_path.c_str(), RTLD_LAZY);
  ASSERT_NE(handle, nullptr);
  DEFER(dlclose(handle));

  // Should be able to call symbols in the dyntab using dlsym().
  std::string dyn_func = "dyn_func";
  ASSERT_OK_AND_ASSIGN(auto dyn_func_ptr, DLSym<int()>(handle, dyn_func.c_str()));
  ASSERT_NE(dyn_func_ptr, nullptr);
  EXPECT_EQ(dyn_func_ptr(), 3);

  // Static functions in the symtab are not callable by dlsym().
  std::string static_func = "static_func";
  ASSERT_NOT_OK(DLSym<int()>(handle, static_func.c_str()));

  // Now try to call the symbol in symtab anyways, by hacking it a bit.
  ASSERT_OK_AND_ASSIGN(auto elf_reader, ElfReader::Create(so_path));
  auto sym_addr = elf_reader->SymbolAddress(static_func).value();
  ASSERT_NE(sym_addr, 0);

  uint64_t vmem_start = *(size_t const*)handle;
  uint64_t fptr_addr = vmem_start + sym_addr;
  using F = int();
  auto func3 = reinterpret_cast<F*>(fptr_addr);

  ASSERT_NE(func3, nullptr);
  EXPECT_EQ(func3(), 3);
}

// Tests whether we can call functions from both the dynsym and symtab tables using RawFptrManager.
TEST(RawFptrManager, OpenDynamicLibraryAndGetFunctionPointers) {
  std::filesystem::path so_path =
      px::testing::BazelRunfilePath("src/stirling/obj_tools/testdata/c/library.so");
  const int STACK_SIZE = 65536;
  void* stack = malloc(STACK_SIZE);
  unsigned long flags = SIGCHLD | CLONE_NEWNS;
  printf("this is working");
  auto pid = clone(child_func, (char *)stack + STACK_SIZE, flags, NULL);
  if (pid == -1) {
      FAIL() << absl::Substitute("Call to clone failed: $0", std::strerror(errno));
  }

  // Test a symbol from the .dynsym table.

  auto fptr_manager = std::make_unique<obj_tools::RawFptrManager>(so_path, pid);

  {
    ASSERT_OK_AND_ASSIGN(auto fptr, fptr_manager->RawSymbolToFptr<int()>("dyn_func"));
    ASSERT_TRUE(fptr != nullptr);
    EXPECT_EQ(fptr(), 3);
  }

  // Test a symbol from the .symtab table.
  {
    ASSERT_OK_AND_ASSIGN(auto fptr, fptr_manager->RawSymbolToFptr<int()>("static_func"));
    ASSERT_TRUE(fptr != nullptr);
    EXPECT_EQ(fptr(), 3);
  }

  int status;
  wait(&status);
  LOG(WARNING) << absl::Substitute("Child exited with $0", status);
}

}  // namespace obj_tools
}  // namespace stirling
}  // namespace px
