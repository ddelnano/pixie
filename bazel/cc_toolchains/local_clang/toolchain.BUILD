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

load("@rules_cc//cc:defs.bzl", "cc_toolchain")
load("@unix_cc_toolchain_config//:cc_toolchain_config.bzl", "cc_toolchain_config")

# ============================================================================
# libc++ files (bundled in LLVM release, symlinked to libcxx/)
# ============================================================================

filegroup(
    name = "libcxx_compiler_files",
    srcs = glob(["{libcxx_path}/include/**"]),
    visibility = ["//visibility:public"],
)

filegroup(
    name = "libcxx_linker_files",
    srcs = glob(["{libcxx_path}/lib/**"]),
    visibility = ["//visibility:public"],
)

filegroup(
    name = "libcxx_all_files",
    srcs = [
        ":libcxx_compiler_files",
        ":libcxx_linker_files",
    ],
    visibility = ["//visibility:public"],
)

# ============================================================================
# Toolchain files
# ============================================================================

filegroup(
    name = "toolchain_compiler_files",
    srcs = [
        ":as",
        ":clang",
        ":toolchain_include",
    ],
    visibility = ["//visibility:public"],
)

filegroup(
    name = "toolchain_linker_files",
    srcs = [
        ":ar",
        ":clang",
        ":lld",
        ":toolchain_lib",
    ],
    visibility = ["//visibility:public"],
)

filegroup(
    name = "toolchain_ar_files",
    srcs = [":ar"],
    visibility = ["//visibility:public"],
)

filegroup(
    name = "toolchain_as_files",
    srcs = [":as"],
    visibility = ["//visibility:public"],
)

filegroup(
    name = "toolchain_dwp_files",
    srcs = [":dwp"],
    visibility = ["//visibility:public"],
)

filegroup(
    name = "toolchain_objcopy_files",
    srcs = [":objcopy"],
    visibility = ["//visibility:public"],
)

filegroup(
    name = "toolchain_nm_files",
    srcs = [":nm"],
    visibility = ["//visibility:public"],
)

filegroup(
    name = "toolchain_strip_files",
    srcs = [":strip"],
    visibility = ["//visibility:public"],
)

filegroup(
    name = "toolchain_include",
    srcs = glob([
        "{toolchain_path}/lib/clang/{clang_major_version}/include/**",
    ]),
)

filegroup(
    name = "toolchain_lib",
    srcs = glob([
        "{toolchain_path}/lib/{target_triple}/libc++*",
        "{toolchain_path}/lib/clang/{clang_major_version}/lib/{target_triple}/**",
    ]),
)

filegroup(
    name = "clang",
    srcs = [
        "{toolchain_path}/bin/clang",
        "{toolchain_path}/bin/clang++",
        "{toolchain_path}/bin/clang-cpp",
        "{toolchain_path}/bin/clang-{clang_major_version}",
    ],
)

filegroup(
    name = "lld",
    srcs = [
        "{toolchain_path}/bin/ld.lld",
        "{toolchain_path}/bin/lld",
    ],
)

[
    filegroup(
        name = binary,
        srcs = [
            "{toolchain_path}/bin/llvm-" + binary,
        ],
    )
    for binary in [
        "ar",
        "as",
        "cov",
        "objcopy",
        "nm",
        "strip",
        "dwp",
    ]
]

filegroup(
    name = "toolchain_all_files",
    srcs = [
        ":toolchain_ar_files",
        ":toolchain_as_files",
        ":toolchain_compiler_files",
        ":toolchain_dwp_files",
        ":toolchain_linker_files",
        ":toolchain_objcopy_files",
        ":toolchain_nm_files",
        ":toolchain_strip_files",
    ],
    visibility = ["//visibility:public"],
)

# ============================================================================
# Toolchain configuration
# ============================================================================

toolchain_identifier = "{name}_toolchain"

tool_paths = {
    "ar": "{toolchain_path}/bin/llvm-ar",
    "cpp": "{toolchain_path}/bin/clang-cpp",
    "dwp": "{toolchain_path}/bin/llvm-dwp",
    "gcc": "{toolchain_path}/bin/clang-{clang_major_version}",
    "ld": "{toolchain_path}/bin/ld.lld",
    "llvm-cov": "{toolchain_path}/bin/llvm-cov",
    "nm": "{toolchain_path}/bin/llvm-nm",
    "objcopy": "{toolchain_path}/bin/llvm-objcopy",
    "objdump": "{toolchain_path}/bin/llvm-objdump",
    "strip": "{toolchain_path}/bin/llvm-strip",
}

# Include paths for local LLVM installation
# Note: LLVM 21+ uses lib/clang/<major_version>/include for clang builtins
# and include/c++/v1 for libc++ headers. Target-specific headers are in
# include/<target-triple>/c++/v1/
includes = [
    "{toolchain_path}/lib/clang/{clang_major_version}/include",
    "{sysroot_include_prefix}/usr/local/include",
    "{sysroot_include_prefix}/usr/include/x86_64-linux-gnu",
    "{sysroot_include_prefix}/usr/include",
    "{sysroot_include_prefix}/usr/include/c++/12",
    "{sysroot_include_prefix}/usr/include/x86_64-linux-gnu/c++/12",
    "{sysroot_include_prefix}/usr/include/c++/12/backward",
    "{toolchain_path}/include/{target_triple}/c++/v1",
    "{libcxx_path}/include/c++/v1",
]

cc_toolchain_config(
    name = "toolchain_config",
    abi_libc_version = "{libc_version}",
    abi_version = "clang",
    builtin_sysroot = "{sysroot_path}",
    compile_flags = [
        "-target",
        "{target_arch}-linux-gnu",
        "-fstack-protector",
        "-Wall",
        "-Wthread-safety",
        "-Wself-assign",
        "-Wunused-but-set-parameter",
        "-fcolor-diagnostics",
        "-fno-omit-frame-pointer",
    ],
    compiler = "clang",
    coverage_compile_flags = ["--coverage"],
    coverage_link_flags = ["--coverage"],
    cpu = "{target_arch}",
    cxx_builtin_include_directories = includes,
    cxx_flags = [
        "-std=c++20",
        "-fPIC",
    ],
    dbg_compile_flags = ["-g"],
    enable_sanitizers = not {use_for_host_tools},
    host_system_name = "{host_arch}-unknown-linux-{host_abi}",
    libclang_rt_path = "external/{this_repo}/{toolchain_path}/lib/clang/{clang_major_version}/lib/{target_triple}",
    libcxx_path = "external/{this_repo}/{libcxx_path}",
    link_flags = [
        "-target",
        "{target_arch}-linux-gnu",
        "-static-libgcc",
        "-fuse-ld=lld",
        "-Wl,-no-as-needed",
        "-Wl,-z,relro,-z,now",
        "-Bexternal/{this_repo}/{toolchain_path}/bin",
        "-lm",
    ] + (["-no-pie"] if {use_for_host_tools} else []),
    opt_compile_flags = [
        "-g0",
        "-O2",
        "-D_FORTIFY_SOURCE=1",
        "-DNDEBUG",
        "-ffunction-sections",
        "-fdata-sections",
    ],
    opt_link_flags = ["-Wl,--gc-sections"],
    supports_start_end_lib = True,
    target_libc = "{libc_version}",
    target_system_name = "{target_arch}-unknown-linux-{target_abi}",
    tool_paths = tool_paths,
    toolchain_identifier = toolchain_identifier,
    unfiltered_compile_flags = [
        "-no-canonical-prefixes",
        "-Wno-builtin-macro-redefined",
        "-D__DATE__=\"redacted\"",
        "-D__TIMESTAMP__=\"redacted\"",
        "-D__TIME__=\"redacted\"",
    ],
)

# ============================================================================
# Combined filegroups
# ============================================================================

filegroup(
    name = "all_files",
    srcs = [
        ":libcxx_all_files",
        ":toolchain_all_files",
    ] + (["@{sysroot_repo}//:all_files"] if "{sysroot_path}" else []),
)

filegroup(
    name = "ar_files",
    srcs = [
        ":toolchain_ar_files",
    ],
)

filegroup(
    name = "as_files",
    srcs = [
        ":toolchain_as_files",
    ],
)

filegroup(
    name = "compiler_files",
    srcs = [
        ":libcxx_compiler_files",
        ":toolchain_compiler_files",
    ] + (["@{sysroot_repo}//:compiler_files"] if "{sysroot_path}" else []),
)

filegroup(
    name = "dwp_files",
    srcs = [
        ":toolchain_dwp_files",
    ],
)

filegroup(
    name = "linker_files",
    srcs = [
        ":libcxx_linker_files",
        ":toolchain_linker_files",
    ] + (["@{sysroot_repo}//:linker_files"] if "{sysroot_path}" else []),
)

filegroup(
    name = "objcopy_files",
    srcs = [
        ":toolchain_objcopy_files",
    ],
)

filegroup(
    name = "strip_files",
    srcs = [
        ":toolchain_strip_files",
    ],
)

# ============================================================================
# cc_toolchain and toolchain definitions
# ============================================================================

cc_toolchain(
    name = "cc_toolchain",
    all_files = ":all_files",
    ar_files = ":ar_files",
    as_files = ":as_files",
    compiler_files = ":compiler_files",
    dwp_files = ":dwp_files",
    linker_files = ":linker_files",
    module_map = None,
    objcopy_files = ":objcopy_files",
    strip_files = ":strip_files",
    supports_param_files = 1,
    toolchain_config = "toolchain_config",
    toolchain_identifier = toolchain_identifier,
)

toolchain(
    name = "toolchain",
    exec_compatible_with = [
        "@platforms//cpu:{host_arch}",
        "@platforms//os:linux",
    ],
    target_compatible_with = [
        "@platforms//cpu:{target_arch}",
        "@platforms//os:linux",
    ] + (["@px//bazel/cc_toolchains:is_exec_true"] if {use_for_host_tools} else ["@px//bazel/cc_toolchains:is_exec_false"]),
    # Note: For local testing toolchain, we don't restrict target_settings
    # to make it easier to use with different configurations.
    toolchain = ":cc_toolchain",
    toolchain_type = "@bazel_tools//tools/cpp:toolchain_type",
)
