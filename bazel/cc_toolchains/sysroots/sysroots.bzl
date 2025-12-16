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

load("@bazel_skylib//lib:selects.bzl", "selects")
load("//bazel/cc_toolchains:utils.bzl", "abi")

SYSROOT_LOCATIONS = dict(
    sysroot_x86_64_glibc2_36_runtime = dict(
        sha256 = "86d26de09f25f3ba028c9ed3d83d0bef8189ca37e224567367fe8362432ca615",
        strip_prefix = "",
        urls = [
            "https://github.com/ddelnano/dev-artifacts/releases/download/sysroots%2Fpl9/sysroot-amd64-runtime.tar.gz",
        ],
    ),
    sysroot_x86_64_glibc2_36_build = dict(
        sha256 = "eb8826964b8f83e6ceb16194b892a47c8c7cba212e44205f3f2d94c4c9150f8f",
        strip_prefix = "",
        urls = [
            "https://github.com/ddelnano/dev-artifacts/releases/download/sysroots%2Fpl9/sysroot-amd64-build.tar.gz",
        ],
    ),
    sysroot_x86_64_glibc2_36_test = dict(
        sha256 = "9dd852040ddaa867f6577564f1510f86104ed1d65300b8a147f3707fc50e833c",
        strip_prefix = "",
        urls = [
            "https://github.com/ddelnano/dev-artifacts/releases/download/sysroots%2Fpl9/sysroot-amd64-test.tar.gz",
        ],
    ),
    sysroot_x86_64_glibc2_36_debug = dict(
        sha256 = "491a1267addd25f42ebd120d3133fd94676dd5915519ae64de85873d7bcb8125",
        strip_prefix = "",
        urls = [
            "https://github.com/ddelnano/dev-artifacts/releases/download/sysroots%2Fpl9/sysroot-amd64-debug.tar.gz",
        ],
    ),
    sysroot_aarch64_glibc2_36_runtime = dict(
        sha256 = "9e578d28df84cdba999a7fd614ec7c230ca018a2fa13e731ca4a6eb1e00188e1",
        strip_prefix = "",
        urls = [
            "https://github.com/ddelnano/dev-artifacts/releases/download/sysroots%2Fpl9/sysroot-arm64-runtime.tar.gz",
        ],
    ),
    sysroot_aarch64_glibc2_36_build = dict(
        sha256 = "9e562cda54dacdebe9ead5bd5b2d6ed91312f033d8f54f035aa0e67c0e2a91b0",
        strip_prefix = "",
        urls = [
            "https://github.com/ddelnano/dev-artifacts/releases/download/sysroots%2Fpl9/sysroot-arm64-build.tar.gz",
        ],
    ),
    sysroot_aarch64_glibc2_36_test = dict(
        sha256 = "e1e0de9a5ac92fc3d9fd9207d5e3f310435e58bcfeb2546685cbe46c75744843",
        strip_prefix = "",
        urls = [
            "https://github.com/ddelnano/dev-artifacts/releases/download/sysroots%2Fpl9/sysroot-arm64-test.tar.gz",
        ],
    ),
    sysroot_aarch64_glibc2_36_debug = dict(
        sha256 = "455044dc0db5026b4704637b28e99fa1069bc77ee959f057ae2da3975f0de2cd",
        strip_prefix = "",
        urls = [
            "https://github.com/ddelnano/dev-artifacts/releases/download/sysroots%2Fpl9/sysroot-arm64-debug.tar.gz",
        ],
    ),
)

_sysroot_architectures = ["aarch64", "x86_64"]
_sysroot_libc_versions = ["glibc2_36"]
_sysroot_variants = ["runtime", "build", "test", "debug"]

def _sysroot_repo_name(target_arch, libc_version, variant):
    name = "sysroot_{target_arch}_{libc_version}_{variant}".format(
        target_arch = target_arch,
        libc_version = libc_version,
        variant = variant,
    )
    if name in SYSROOT_LOCATIONS:
        return name
    return ""

def _sysroot_setting_name(target_arch, libc_version):
    return "using_sysroot_{target_arch}_{libc_version}".format(
        target_arch = target_arch,
        libc_version = libc_version,
    )

def _sysroot_repo_impl(rctx):
    loc = SYSROOT_LOCATIONS[rctx.attr.name]
    tar_path = "sysroot.tar.gz"
    rctx.download(
        url = loc["urls"],
        output = tar_path,
        sha256 = loc["sha256"],
    )

    rctx.extract(
        tar_path,
        stripPrefix = loc.get("strip_prefix", ""),
    )

    rctx.template(
        "BUILD.bazel",
        Label("@px//bazel/cc_toolchains/sysroots/{variant}:sysroot.BUILD".format(variant = rctx.attr.variant)),
        substitutions = {
            "{abi}": abi(rctx.attr.target_arch, rctx.attr.libc_version),
            "{libc_version}": rctx.attr.libc_version,
            "{path_to_this_repo}": "external/" + rctx.attr.name,
            "{tar_path}": tar_path,
            "{target_arch}": rctx.attr.target_arch,
        },
    )

_sysroot_repo = repository_rule(
    implementation = _sysroot_repo_impl,
    attrs = {
        "libc_version": attr.string(mandatory = True, doc = "Libc version of the sysroot"),
        "target_arch": attr.string(mandatory = True, doc = "CPU Architecture of the sysroot"),
        "variant": attr.string(mandatory = True, doc = "Use case variant of the sysroot. One of 'runtime', 'build', or 'test'"),
    },
)

SysrootInfo = provider(
    doc = "Information about a sysroot.",
    fields = ["files", "architecture", "path", "tar"],
)

def _sysroot_toolchain_impl(ctx):
    return [
        platform_common.ToolchainInfo(
            sysroot = SysrootInfo(
                files = ctx.attr.files.files,
                architecture = ctx.attr.architecture,
                path = ctx.attr.path,
                tar = ctx.attr.tar.files,
            ),
        ),
    ]

sysroot_toolchain = rule(
    implementation = _sysroot_toolchain_impl,
    attrs = {
        "architecture": attr.string(mandatory = True, doc = "CPU architecture targeted by this sysroot"),
        "files": attr.label(mandatory = True, doc = "All sysroot files"),
        "path": attr.string(mandatory = True, doc = "Path to sysroot relative to execroot"),
        "tar": attr.label(mandatory = True, doc = "Sysroot tar, used to avoid repacking the sysroot as a tar for docker images."),
    },
)

def _pl_sysroot_deps():
    toolchains = []
    for target_arch in _sysroot_architectures:
        for libc_version in _sysroot_libc_versions:
            for variant in _sysroot_variants:
                repo = _sysroot_repo_name(target_arch, libc_version, variant)
                _sysroot_repo(
                    name = repo,
                    target_arch = target_arch,
                    libc_version = libc_version,
                    variant = variant,
                )
                toolchains.append("@{repo}//:toolchain".format(repo = repo))
    native.register_toolchains(*toolchains)

def _pl_sysroot_settings():
    for target_arch in _sysroot_architectures:
        for libc_version in _sysroot_libc_versions:
            selects.config_setting_group(
                name = _sysroot_setting_name(target_arch, libc_version),
                match_all = [
                    "@platforms//cpu:" + target_arch,
                    "//bazel/cc_toolchains:libc_version_" + libc_version,
                ],
                visibility = ["//visibility:public"],
            )

sysroot_repo_name = _sysroot_repo_name
sysroot_libc_versions = _sysroot_libc_versions
sysroot_architectures = _sysroot_architectures
pl_sysroot_settings = _pl_sysroot_settings
pl_sysroot_deps = _pl_sysroot_deps
