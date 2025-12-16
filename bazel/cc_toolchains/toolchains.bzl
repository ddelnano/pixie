# Copyright 2018- The Pixie Authors.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# SPDX-License-Identifier: Apache-2.0

load("//bazel/cc_toolchains:clang.bzl", "clang_register_toolchain")
load("//bazel/cc_toolchains:local_clang.bzl", "local_clang_register_toolchain")

def _pl_register_cc_toolchains():
    clang_register_toolchain(
        name = "clang-21.1-x86_64",
        toolchain_repo = "com_llvm_clang_21",
        target_arch = "x86_64",
        clang_version = "21.1.7",
        libc_version = "glibc_host",
    )
    clang_register_toolchain(
        name = "clang-21.1-x86_64-glibc2.36-sysroot",
        toolchain_repo = "com_llvm_clang_21",
        target_arch = "x86_64",
        clang_version = "21.1.7",
        libc_version = "glibc2_36",
    )
    clang_register_toolchain(
        name = "clang-21.1-aarch64-glibc2.36-sysroot",
        toolchain_repo = "com_llvm_clang_21",
        target_arch = "aarch64",
        clang_version = "21.1.7",
        libc_version = "glibc2_36",
    )
    clang_register_toolchain(
        name = "clang-21.1-exec",
        toolchain_repo = "com_llvm_clang_21",
        target_arch = "x86_64",
        clang_version = "21.1.7",
        libc_version = "glibc_host",
        use_for_host_tools = True,
    )

    native.register_toolchains(
        "//bazel/cc_toolchains:cc-toolchain-gcc-x86_64-gnu",
    )

def _pl_register_local_clang_toolchain(
        name = "clang-local",
        llvm_path = "/home/dev/LLVM-21.1.7-Linux-X64",
        clang_version = "21.1.7"):
    """Register a local LLVM/Clang toolchain for testing.

    This is useful for testing new clang versions before building a
    custom Pixie clang distribution.

    Args:
        name: Name for the toolchain (default: "clang-local")
        llvm_path: Absolute path to local LLVM installation
        clang_version: Clang version string (e.g., "21.1.7")

    Usage in WORKSPACE:
        load("//bazel/cc_toolchains:toolchains.bzl", "pl_register_local_clang_toolchain")
        pl_register_local_clang_toolchain()

    Then build with:
        bazel build --extra_toolchains=@clang-local//:toolchain //your:target
    """
    local_clang_register_toolchain(
        name = name,
        llvm_path = llvm_path,
        clang_version = clang_version,
        target_arch = "x86_64",
        libc_version = "glibc_host",
        use_for_host_tools = False,
    )
    local_clang_register_toolchain(
        name = "clang-exec",
        llvm_path = llvm_path,
        clang_version = clang_version,
        target_arch = "x86_64",
        libc_version = "glibc_host",
        use_for_host_tools = True,
    )

pl_register_cc_toolchains = _pl_register_cc_toolchains
pl_register_local_clang_toolchain = _pl_register_local_clang_toolchain
