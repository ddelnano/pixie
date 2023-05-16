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

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_file")

kernel_build_date = 20230516231333
kernel_catalog = {
    "4.14.254": "17e9ec8e081f98f38d23e5deb4ed0d29cc9785e19e842289d2f8ecd4f0263605",
    "4.19.254": "2246f6f7a7949b721e228b94affd93fdb3a6e4262a589f870df232624daf6123",
    "5.4.235": "29ec911b99a55deebdc1538903b350c93dc33fe2e4d3a92dbd4020f6992d8aba",
    "5.10.173": "d9033d193d44a3abcd1e14ab7757052e1ba104753e9a45f371356aac222f7381",
    "5.15.101": "50d6150186856211ec0a3fc752568daf89f569c0c7b911c53cba402dd330041a",
    "6.1.18": "29f85b949a57b4b7f3c88c4c5459420b61247a71c05a7c1aa9a8d038fdc7a80b",
}

def kernel_version_to_name(version):
    return "linux_build_{}_x86_64".format(version.replace(".", "_"))

def qemu_kernel_deps():
    for version, sha in kernel_catalog.items():
        http_file(
            name = kernel_version_to_name(version),
            url = "https://storage.googleapis.com/pixie-dev-public/kernel-build/{}/linux-build-{}.tar.gz".format(kernel_build_date, version),
            sha256 = sha,
            downloaded_file_path = "linux-build.tar.gz",
        )

def qemu_kernel_list():
    # Returns a list of kernels, including "oldest" and "latest".
    return ["oldest"] + kernel_catalog.keys() + ["latest"]

def _kernel_sort_key(version):
    # This assumes that there are no more than 1024 minor or patch versions.
    maj, minor, patch = version.split(".")
    return (int(maj) * (1024 * 1024)) + (int(minor) * (1024)) + int(patch)

def get_dep_name(version):
    return "@{}//file:linux-build.tar.gz".format(kernel_version_to_name(version))

def qemu_image_to_deps():
    # Returns a dict which has the kernel names, deps.
    deps = {}

    for version in kernel_catalog.keys():
        deps[version] = get_dep_name(version)

    # We need to add the oldest and latest versions.
    kernels_sorted = sorted(kernel_catalog.keys(), key = _kernel_sort_key)
    kernel_oldest = kernels_sorted[0]
    kernel_latest = kernels_sorted[-1]
    deps["oldest"] = get_dep_name(kernel_oldest)
    deps["latest"] = get_dep_name(kernel_latest)

    return deps

def kernel_flag_name(version):
    return "kernel_version_{}".format(version.replace(".", "_"))
