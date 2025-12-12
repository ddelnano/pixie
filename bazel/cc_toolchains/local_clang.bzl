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

"""Repository rules for using a local LLVM/Clang installation as a toolchain."""

load("//bazel/cc_toolchains:settings.bzl", "HOST_GLIBC_VERSION")
load("//bazel/cc_toolchains:utils.bzl", "abi")
load("//bazel/cc_toolchains/sysroots:sysroots.bzl", "sysroot_repo_name")

def _local_clang_toolchain_impl(rctx):
    # Create a symlink to the local LLVM installation
    toolchain_path = "toolchain"
    rctx.symlink(rctx.attr.llvm_path, toolchain_path)

    # For local LLVM releases, libc++ is bundled in the same directory.
    # However, the lib structure is different: LLVM 21+ puts libc++ at
    # lib/<target-triple>/ instead of just lib/. toolchain_features.bzl
    # expects libs at {libcxx_path}/lib/, so we create a separate libcxx
    # directory with symlinks to make it compatible.
    libcxx_path = "libcxx"
    target_triple = "{arch}-unknown-linux-gnu".format(arch = rctx.attr.target_arch)

    # Create libcxx directory structure that matches expected layout
    rctx.execute(["mkdir", "-p", libcxx_path + "/include/c++"])
    rctx.execute(["mkdir", "-p", libcxx_path + "/lib"])

    # Symlink the include directory (headers are at include/c++/v1)
    rctx.symlink(toolchain_path + "/include/c++/v1", libcxx_path + "/include/c++/v1")

    # Symlink the target-specific include directory if it exists
    target_include_path = toolchain_path + "/include/" + target_triple + "/c++/v1"
    if rctx.path(target_include_path).exists:
        rctx.execute(["mkdir", "-p", libcxx_path + "/include/" + target_triple + "/c++"])
        rctx.symlink(target_include_path, libcxx_path + "/include/" + target_triple + "/c++/v1")

    # Symlink lib files from lib/<target-triple>/ to lib/
    # This makes the structure compatible with toolchain_features.bzl
    target_lib_path = toolchain_path + "/lib/" + target_triple
    for lib_file in ["libc++.a", "libc++abi.a", "libc++.so", "libc++.so.1", "libc++.so.1.0",
                     "libc++abi.so", "libc++abi.so.1", "libc++abi.so.1.0",
                     "libunwind.a", "libunwind.so", "libunwind.so.1", "libunwind.so.1.0"]:
        src = target_lib_path + "/" + lib_file
        if rctx.path(src).exists:
            rctx.symlink(src, libcxx_path + "/lib/" + lib_file)

    sysroot_repo = sysroot_repo_name(rctx.attr.target_arch, rctx.attr.libc_version, "build")
    sysroot_path = ""
    sysroot_include_prefix = ""
    if sysroot_repo:
        sysroot_path = "external/{repo}".format(repo = sysroot_repo)
        sysroot_include_prefix = "%sysroot%"

    target_libc_constraints = ["@px//bazel/cc_toolchains:libc_version_{libc_version}".format(libc_version = rctx.attr.libc_version)]

    # Allow host toolchains to be selected regardless of target libc version
    if rctx.attr.use_for_host_tools:
        target_libc_constraints = []

    # Get clang major version
    clang_major_version = rctx.attr.clang_version.split(".")[0]

    # Determine the target triple for lib paths (LLVM 21+ uses this structure)
    target_triple = "{arch}-unknown-linux-gnu".format(arch = rctx.attr.target_arch)

    rctx.template(
        "BUILD.bazel",
        Label("@px//bazel/cc_toolchains/local_clang:toolchain.BUILD"),
        substitutions = {
            "{clang_major_version}": clang_major_version,
            "{clang_version}": rctx.attr.clang_version,
            "{host_abi}": abi(rctx.attr.host_arch, rctx.attr.host_libc_version),
            "{host_arch}": rctx.attr.host_arch,
            "{libc_version}": rctx.attr.libc_version,
            "{libcxx_path}": libcxx_path,
            "{name}": rctx.attr.name,
            "{sysroot_include_prefix}": sysroot_include_prefix,
            "{sysroot_path}": sysroot_path,
            "{sysroot_repo}": sysroot_repo,
            "{target_abi}": abi(rctx.attr.target_arch, rctx.attr.libc_version),
            "{target_arch}": rctx.attr.target_arch,
            "{target_libc_constraints}": str(target_libc_constraints),
            "{target_triple}": target_triple,
            "{this_repo}": rctx.attr.name,
            "{toolchain_path}": toolchain_path,
            "{use_for_host_tools}": str(rctx.attr.use_for_host_tools),
        },
    )

local_clang_toolchain = repository_rule(
    _local_clang_toolchain_impl,
    attrs = dict(
        llvm_path = attr.string(mandatory = True, doc = "Absolute path to local LLVM installation"),
        target_arch = attr.string(mandatory = True),
        libc_version = attr.string(mandatory = True),
        host_arch = attr.string(mandatory = True),
        host_libc_version = attr.string(mandatory = True),
        clang_version = attr.string(mandatory = True),
        use_for_host_tools = attr.bool(mandatory = True),
    ),
)

def _local_clang_register_toolchain(
        name,
        llvm_path,
        clang_version,
        target_arch = "x86_64",
        libc_version = HOST_GLIBC_VERSION,
        host_arch = "x86_64",
        host_libc_version = HOST_GLIBC_VERSION,
        use_for_host_tools = False):
    """Register a toolchain using a local LLVM installation.

    Args:
        name: Name for this toolchain
        llvm_path: Absolute path to the local LLVM installation directory
        clang_version: Version string (e.g., "21.1.7")
        target_arch: Target architecture (default: "x86_64")
        libc_version: libc version constraint (default: HOST_GLIBC_VERSION)
        host_arch: Host architecture (default: "x86_64")
        host_libc_version: Host libc version (default: HOST_GLIBC_VERSION)
        use_for_host_tools: Whether this toolchain is for host tools (default: False)
    """
    local_clang_toolchain(
        name = name,
        llvm_path = llvm_path,
        target_arch = target_arch,
        libc_version = libc_version,
        host_arch = host_arch,
        host_libc_version = host_libc_version,
        clang_version = clang_version,
        use_for_host_tools = use_for_host_tools,
    )
    native.register_toolchains("@{name}//:toolchain".format(name = name))

local_clang_register_toolchain = _local_clang_register_toolchain
