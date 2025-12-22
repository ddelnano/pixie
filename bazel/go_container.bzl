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

"""
Bazel rules and macros for generating Go container test libraries.

This module provides a custom rule and macros to generate C++ header files
for Go container test fixtures at build time. This eliminates the need for
manually maintaining per-version header files.

Usage:
    In BUILD.bazel:
        load("//bazel:go_container.bzl", "go_container_libraries")

        go_container_libraries(
            use_case = "grpc_server",
            versions = ["1.23", "1.24"],
            legacy_versions = ["1.18", "1.19"],
        )
"""

load("//bazel:pl_build_system.bzl", "pl_cc_test_library")

# Template for container header content
_CONTAINER_HEADER_TEMPLATE = """\
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

#pragma once

#include <string>

#include "src/common/testing/test_environment.h"
#include "src/common/testing/test_utils/container_runner.h"

namespace px {{
namespace stirling {{
namespace testing {{

class {class_name} : public ContainerRunner {{
 public:
  {class_name}()
      : ContainerRunner(::px::testing::BazelRunfilePath(kBazelImageTar), kContainerNamePrefix,
                        kReadyMessage) {{}}

 private:
  static constexpr std::string_view kBazelImageTar =
      "{tar_path}";
  static constexpr std::string_view kContainerNamePrefix = "{container_prefix}";
  static constexpr std::string_view kReadyMessage = {ready_message};
}};

}}  // namespace testing
}}  // namespace stirling
}}  // namespace px
"""

# Configuration for each use case
# Keys: use_case name
# Values: dict with container_prefix, ready_message, tar patterns
_GO_CONTAINER_CONFIGS = {
    "grpc_server": {
        "container_prefix": "grpc_server",
        "ready_message": '"Starting HTTP/2 server"',
        "tar_pattern_modern": "src/stirling/testing/demo_apps/go_grpc_tls_pl/server/golang_{version}_grpc_tls_server.tar",
        "tar_pattern_legacy": "src/stirling/source_connectors/socket_tracer/testing/containers/golang_{version}_grpc_server_with_buildinfo.tar",
        "class_suffix": "GRPCServerContainer",
    },
    "grpc_client": {
        "container_prefix": "grpc_client",
        "ready_message": '""',
        "tar_pattern_modern": "src/stirling/testing/demo_apps/go_grpc_tls_pl/client/golang_{version}_grpc_tls_client.tar",
        "tar_pattern_legacy": None,
        "class_suffix": "GRPCClientContainer",
    },
    "tls_server": {
        "container_prefix": "https_server",
        "ready_message": '"Starting HTTPS service"',
        "tar_pattern_modern": "src/stirling/testing/demo_apps/go_https/server/golang_{version}_https_server.tar",
        "tar_pattern_legacy": "src/stirling/source_connectors/socket_tracer/testing/containers/golang_{version}_https_server_with_buildinfo.tar",
        "class_suffix": "TLSServerContainer",
    },
    "tls_client": {
        "container_prefix": "https_client",
        "ready_message": 'R"({"status":"ok"})"',
        "tar_pattern_modern": "src/stirling/testing/demo_apps/go_https/client/golang_{version}_https_client.tar",
        "tar_pattern_legacy": None,
        "class_suffix": "TLSClientContainer",
    },
}

def _version_to_class_prefix(version):
    """Convert version string to class name prefix.

    Args:
        version: Go SDK version string (e.g., "1.24", "1.23.11")

    Returns:
        Class name prefix (e.g., "Go1_24_", "GoBoringCrypto")
    """

    # BoringCrypto versions (e.g., "1.23.11") get special naming
    if version.count(".") == 2:
        return "GoBoringCrypto"
    return "Go" + version.replace(".", "_") + "_"

def _version_to_label_suffix(version):
    """Convert version string to bazel label suffix.

    Args:
        version: Go SDK version string (e.g., "1.24", "1.23.11")

    Returns:
        Label suffix (e.g., "1_24", "boringcrypto")
    """

    # BoringCrypto versions (e.g., "1.23.11") use "boringcrypto" in labels
    if version.count(".") == 2:
        return "boringcrypto"
    return version.replace(".", "_")

def _go_container_header_impl(ctx):
    """Generate a Go container header file."""
    output = ctx.actions.declare_file(ctx.attr.header_name)

    ctx.actions.write(
        output = output,
        content = _CONTAINER_HEADER_TEMPLATE.format(
            class_name = ctx.attr.class_name,
            tar_path = ctx.attr.tar_path,
            container_prefix = ctx.attr.container_prefix,
            ready_message = ctx.attr.ready_message,
        ),
    )
    return [DefaultInfo(files = depset([output]))]

go_container_header = rule(
    implementation = _go_container_header_impl,
    attrs = {
        "class_name": attr.string(mandatory = True),
        "container_prefix": attr.string(mandatory = True),
        "header_name": attr.string(mandatory = True),
        "ready_message": attr.string(mandatory = True),
        "tar_path": attr.string(mandatory = True),
    },
)

def go_container_library(name, use_case, version, is_legacy = False):
    """
    Create a container library for a specific Go version and use case.

    This macro generates a C++ header file and wraps it in a pl_cc_test_library
    that can be used as a dependency in tests.

    Args:
        name: Target name for the library
        use_case: One of "grpc_server", "grpc_client", "tls_server", "tls_client"
        version: Go SDK version (e.g., "1.24", "1.23.11")
        is_legacy: Whether to use legacy tar path pattern (for older Go versions)
    """
    config = _GO_CONTAINER_CONFIGS[use_case]
    label_suffix = _version_to_label_suffix(version)
    class_prefix = _version_to_class_prefix(version)

    # Determine tar path pattern
    if is_legacy and config["tar_pattern_legacy"]:
        tar_pattern = config["tar_pattern_legacy"]
    else:
        tar_pattern = config["tar_pattern_modern"]

    tar_path = tar_pattern.format(version = label_suffix)

    # Class name: Go{version}_{UseCase}Container or GoBoringCrypto{UseCase}Container
    class_name = class_prefix + config["class_suffix"]

    header_name = "go_{}_{}_container.h".format(label_suffix, use_case)

    # Generate the header
    go_container_header(
        name = name + "_header",
        header_name = header_name,
        class_name = class_name,
        tar_path = tar_path,
        container_prefix = config["container_prefix"],
        ready_message = config["ready_message"],
    )

    # Parse tar path to get the Bazel label
    # e.g., "src/stirling/testing/demo_apps/go_grpc_tls_pl/server/golang_1_24_grpc_tls_server.tar"
    # becomes "//src/stirling/testing/demo_apps/go_grpc_tls_pl/server:golang_1_24_grpc_tls_server.tar"
    tar_dir = tar_path.rsplit("/", 1)[0]
    tar_file = tar_path.rsplit("/", 1)[1]
    tar_label = "//" + tar_dir + ":" + tar_file

    # Create the test library
    pl_cc_test_library(
        name = name,
        hdrs = [":" + name + "_header"],
        data = [tar_label],
        deps = ["//src/common/testing/test_utils:cc_library"],
    )

def go_container_libraries(use_case, versions, legacy_versions = []):
    """
    Generate container libraries for all versions of a use case.

    This is a convenience macro that generates multiple go_container_library
    targets in a single call.

    Args:
        use_case: One of "grpc_server", "grpc_client", "tls_server", "tls_client"
        versions: List of Go SDK versions to generate libraries for
        legacy_versions: Subset of versions that should use legacy tar paths
    """
    for version in versions:
        is_legacy = version in legacy_versions
        label_suffix = _version_to_label_suffix(version)
        target_name = "go_{}_{}_container".format(label_suffix, use_case)
        go_container_library(
            name = target_name,
            use_case = use_case,
            version = version,
            is_legacy = is_legacy,
        )

def go_container_deps(use_case, versions):
    """
    Generate a list of container library deps for the given versions.

    This is a helper function for use in BUILD files to generate dependency
    lists without manually listing each version.

    Args:
        use_case: One of "grpc_server", "grpc_client", "tls_server", "tls_client"
        versions: List of Go SDK versions to include

    Returns:
        List of Bazel labels for the container libraries
    """
    deps = []
    for version in versions:
        label_suffix = _version_to_label_suffix(version)
        target_name = "go_{}_{}_container".format(label_suffix, use_case)
        deps.append("//src/stirling/source_connectors/socket_tracer/testing/container_images:" + target_name)
    return deps

def all_go_grpc_container_deps(versions):
    """
    Generate deps for all gRPC server and client containers.

    Args:
        versions: List of Go SDK versions to include

    Returns:
        List of Bazel labels for all gRPC container libraries
    """
    return go_container_deps("grpc_server", versions) + go_container_deps("grpc_client", versions)

def all_go_tls_container_deps(versions):
    """
    Generate deps for all TLS server and client containers.

    Args:
        versions: List of Go SDK versions to include

    Returns:
        List of Bazel labels for all TLS container libraries
    """
    return go_container_deps("tls_server", versions) + go_container_deps("tls_client", versions)
