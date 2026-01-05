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

load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "arrow",
    # This list is copied from ARROW_SRCS in
    # https://github.com/apache/arrow/blob/master/cpp/src/arrow/CMakeLists.txt.
    # When updating Arrow, make sure to update this list to ensure that any new files are included.
    #
    # Also make sure to re-generate the following generated files, and include them in the repo.
    #    cpp/src/arrow/ipc/File_generated.h
    #    cpp/src/arrow/ipc/Message_generated.h
    #    cpp/src/arrow/ipc/Schema_generated.h
    #    cpp/src/arrow/ipc/Tensor_generated.h
    #    cpp/src/arrow/ipc/feather_generated.h
    # Try "cmake .; make arrow_dependencies" to do this.
    # Apologies for the hack!
    srcs = [
        # Core array sources (array.cc split into multiple files in Arrow 22.0.0)
        "cpp/src/arrow/array/array_base.cc",
        "cpp/src/arrow/array/array_binary.cc",
        "cpp/src/arrow/array/array_decimal.cc",
        "cpp/src/arrow/array/array_dict.cc",
        "cpp/src/arrow/array/array_nested.cc",
        "cpp/src/arrow/array/array_primitive.cc",
        "cpp/src/arrow/array/array_run_end.cc",
        "cpp/src/arrow/array/builder_adaptive.cc",
        "cpp/src/arrow/array/builder_base.cc",
        "cpp/src/arrow/array/builder_binary.cc",
        "cpp/src/arrow/array/builder_decimal.cc",
        "cpp/src/arrow/array/builder_dict.cc",
        "cpp/src/arrow/array/builder_nested.cc",
        "cpp/src/arrow/array/builder_primitive.cc",
        "cpp/src/arrow/array/builder_run_end.cc",
        "cpp/src/arrow/array/builder_union.cc",
        "cpp/src/arrow/array/concatenate.cc",
        "cpp/src/arrow/array/data.cc",
        "cpp/src/arrow/array/diff.cc",
        "cpp/src/arrow/array/statistics.cc",
        "cpp/src/arrow/array/util.cc",
        "cpp/src/arrow/array/validate.cc",
        
        # Core Arrow sources
        "cpp/src/arrow/buffer.cc",
        "cpp/src/arrow/builder.cc",
        "cpp/src/arrow/chunk_resolver.cc",
        "cpp/src/arrow/chunked_array.cc",
        "cpp/src/arrow/compare.cc",
        "cpp/src/arrow/config.cc",
        "cpp/src/arrow/datum.cc",
        "cpp/src/arrow/device.cc",
        "cpp/src/arrow/device_allocation_type_set.cc",
        "cpp/src/arrow/extension_type.cc",
        "cpp/src/arrow/memory_pool.cc",
        "cpp/src/arrow/pretty_print.cc",
        "cpp/src/arrow/record_batch.cc",
        "cpp/src/arrow/result.cc",
        "cpp/src/arrow/scalar.cc",
        "cpp/src/arrow/sparse_tensor.cc",
        "cpp/src/arrow/status.cc",
        "cpp/src/arrow/table.cc",
        "cpp/src/arrow/table_builder.cc",
        "cpp/src/arrow/tensor.cc",
        "cpp/src/arrow/type.cc",
        "cpp/src/arrow/type_traits.cc",
        "cpp/src/arrow/visitor.cc",
        
        # Util sources (fixed naming: hyphen -> underscore)
        "cpp/src/arrow/util/align_util.cc",
        "cpp/src/arrow/util/async_util.cc",
        "cpp/src/arrow/util/atfork_internal.cc",
        "cpp/src/arrow/util/basic_decimal.cc",
        "cpp/src/arrow/util/bit_block_counter.cc",
        "cpp/src/arrow/util/bit_run_reader.cc",
        "cpp/src/arrow/util/bit_util.cc",
        "cpp/src/arrow/util/bitmap.cc",
        "cpp/src/arrow/util/bitmap_builders.cc",
        "cpp/src/arrow/util/bitmap_ops.cc",
        "cpp/src/arrow/util/bpacking.cc",
        "cpp/src/arrow/util/byte_size.cc",
        "cpp/src/arrow/util/cancel.cc",
        "cpp/src/arrow/util/compression.cc",
        "cpp/src/arrow/util/cpu_info.cc",
        "cpp/src/arrow/util/debug.cc",
        "cpp/src/arrow/util/decimal.cc",
        "cpp/src/arrow/util/delimiting.cc",
        "cpp/src/arrow/util/dict_util.cc",
        "cpp/src/arrow/util/fixed_width_internal.cc",
        "cpp/src/arrow/util/float16.cc",
        "cpp/src/arrow/util/formatting.cc",
        "cpp/src/arrow/util/future.cc",
        "cpp/src/arrow/util/hashing.cc",
        "cpp/src/arrow/util/int_util.cc",
        "cpp/src/arrow/util/io_util.cc",
        "cpp/src/arrow/util/key_value_metadata.cc",
        "cpp/src/arrow/util/list_util.cc",
        "cpp/src/arrow/util/logging.cc",
        "cpp/src/arrow/util/memory.cc",
        "cpp/src/arrow/util/ree_util.cc",
        "cpp/src/arrow/util/string.cc",
        "cpp/src/arrow/util/string_util.cc",
        "cpp/src/arrow/util/task_group.cc",
        "cpp/src/arrow/util/thread_pool.cc",
        "cpp/src/arrow/util/time.cc",
        "cpp/src/arrow/util/tracing.cc",
        "cpp/src/arrow/util/trie.cc",
        "cpp/src/arrow/util/union_util.cc",
        "cpp/src/arrow/util/unreachable.cc",
        "cpp/src/arrow/util/utf8.cc",
        "cpp/src/arrow/util/value_parsing.cc",
        
        # IO sources
        "cpp/src/arrow/io/buffered.cc",
        "cpp/src/arrow/io/caching.cc",
        "cpp/src/arrow/io/compressed.cc",
        "cpp/src/arrow/io/file.cc",
        "cpp/src/arrow/io/interfaces.cc",
        "cpp/src/arrow/io/memory.cc",
        "cpp/src/arrow/io/slow.cc",
        "cpp/src/arrow/io/stdio.cc",
        "cpp/src/arrow/io/transform.cc",
        
        # Tensor sources
        "cpp/src/arrow/tensor/coo_converter.cc",
        "cpp/src/arrow/tensor/csf_converter.cc",
        "cpp/src/arrow/tensor/csx_converter.cc",
        
        # Extension types
        "cpp/src/arrow/extension/bool8.cc",
        "cpp/src/arrow/extension/fixed_shape_tensor.cc",
        "cpp/src/arrow/extension/json.cc",
        "cpp/src/arrow/extension/opaque.cc",
        "cpp/src/arrow/extension/uuid.cc",
        
        # Compute infrastructure
        "cpp/src/arrow/compute/api_aggregate.cc",
        "cpp/src/arrow/compute/api_scalar.cc",
        "cpp/src/arrow/compute/api_vector.cc",
        "cpp/src/arrow/compute/cast.cc",
        "cpp/src/arrow/compute/exec.cc",
        "cpp/src/arrow/compute/expression.cc",
        "cpp/src/arrow/compute/function.cc",
        "cpp/src/arrow/compute/function_internal.cc",
        "cpp/src/arrow/compute/kernel.cc",
        "cpp/src/arrow/compute/ordering.cc",
        "cpp/src/arrow/compute/registry.cc",
        
        # Cast kernels
        "cpp/src/arrow/compute/kernels/scalar_cast_boolean.cc",
        "cpp/src/arrow/compute/kernels/scalar_cast_dictionary.cc",
        "cpp/src/arrow/compute/kernels/scalar_cast_extension.cc",
        "cpp/src/arrow/compute/kernels/scalar_cast_internal.cc",
        "cpp/src/arrow/compute/kernels/scalar_cast_nested.cc",
        "cpp/src/arrow/compute/kernels/scalar_cast_numeric.cc",
        "cpp/src/arrow/compute/kernels/scalar_cast_string.cc",
        "cpp/src/arrow/compute/kernels/scalar_cast_temporal.cc",
        
        # Vector compute kernels
        "cpp/src/arrow/compute/kernels/codegen_internal.cc",
        "cpp/src/arrow/compute/kernels/util_internal.cc",
        "cpp/src/arrow/compute/kernels/vector_hash.cc",
        "cpp/src/arrow/compute/kernels/vector_selection.cc",
        "cpp/src/arrow/compute/kernels/vector_selection_filter_internal.cc",
        "cpp/src/arrow/compute/kernels/vector_selection_internal.cc",
        "cpp/src/arrow/compute/kernels/vector_selection_take_internal.cc",
        "cpp/src/arrow/compute/kernels/vector_sort.cc",
        "cpp/src/arrow/compute/kernels/vector_swizzle.cc",
        
        # Temporal kernels  
        "cpp/src/arrow/compute/kernels/temporal_internal.cc",
        
        # Vendored sources
        "cpp/src/arrow/vendored/datetime/tz.cpp",
        "cpp/src/arrow/vendored/double-conversion/bignum-dtoa.cc",
        "cpp/src/arrow/vendored/double-conversion/bignum.cc",
        "cpp/src/arrow/vendored/double-conversion/cached-powers.cc",
        "cpp/src/arrow/vendored/double-conversion/double-to-string.cc",
        "cpp/src/arrow/vendored/double-conversion/fast-dtoa.cc",
        "cpp/src/arrow/vendored/double-conversion/fixed-dtoa.cc",
        "cpp/src/arrow/vendored/double-conversion/string-to-double.cc",
        "cpp/src/arrow/vendored/double-conversion/strtod.cc",
    ],
    hdrs = glob(
        [
            "cpp/src/arrow/*.h",
            "cpp/src/arrow/**/*.h",
            "cpp/src/arrow/*.hpp",
            "cpp/src/arrow/**/*.hpp",
        ],
        exclude = [
            "cpp/src/arrow/io/hdfs-internal.h",
        ],
    ),
    copts = [
        "-Wno-unused-parameter",
        "-Wno-overloaded-virtual",
        "-Wno-deprecated-declarations",
        "-DUSE_OS_TZDB=1",  # Use OS timezone database to avoid curl dependency
    ],
    includes = [
        "cpp/src",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "@com_github_tencent_rapidjson//:rapidjson",
        "@com_google_absl//absl/base",
        "@com_google_absl//absl/numeric:int128",
        "@com_google_absl//absl/strings",
        "@com_google_flatbuffers//:flatbuffers",
    ],
)
