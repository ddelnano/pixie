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

#include <zlib.h>
#include <string>

#include "src/common/base/base.h"
#include "src/common/zlib/zlib_wrapper.h"

namespace px {
namespace zlib {

StatusOr<std::string> Inflate(std::string_view in, size_t output_block_size) {
  // Handle empty input case separately
  if (in.empty()) {
    return std::string();
  }

  z_stream zs = {};

  if (inflateInit2(&zs, MAX_WBITS + 16) != Z_OK) {
    return error::Internal("inflateInit2 failed while decompressing.");
  }

  zs.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(in.data()));
  zs.avail_in = in.size();

  std::string out;
  int ret;
  bool is_partial = false;

  do {
    size_t current_size = out.size();
    out.resize(current_size + output_block_size);
    zs.next_out = reinterpret_cast<Bytef*>(&out[current_size]);
    zs.avail_out = output_block_size;

    ret = inflate(&zs, Z_NO_FLUSH);

    if (ret == Z_BUF_ERROR && zs.avail_in == 0) {
      // No more input and buffer is full: stop
      is_partial = true;
      break;
    }
    if (ret == Z_DATA_ERROR || ret == Z_MEM_ERROR) {
      inflateEnd(&zs);
      return error::Internal("zlib inflate failed with code $0", ret);
    }
  } while (ret != Z_STREAM_END);

  out.resize(zs.total_out);
  inflateEnd(&zs);

  if (ret != Z_STREAM_END || is_partial) {
    const std::string prefix = "[PARTIAL DECOMPRESSION] ";
    out.append(prefix);
    LOG(WARNING) << "zlib decompression ended without reaching Z_STREAM_END, output is partial.";
  }

  return out;
}

}  // namespace zlib
}  // namespace px
