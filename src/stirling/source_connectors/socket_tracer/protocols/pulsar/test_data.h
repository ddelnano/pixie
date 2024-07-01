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

#include "src/common/base/utils.h"

namespace px {
namespace stirling {
namespace protocols {
namespace pulsar {

#define D(var, str) const std::string_view var = CreateStringView<char>(str)

D(kConnectData,
  "\000\000\000\"\000\000\000\036\b\002\022\032\n\0062.10.1\032\000 "
  "\023*\004noneR\006\b\001\020\001\030\001");
D(kConnectBeginsWith2ExtraBytes,
  "\001\002\000\000\000\"\000\000\000\036\b\002\022\032\n\0062.10.1\032\000 "
  "\023*\004noneR\006\b\001\020\001\030\001");
D(kProduceMessage,
  "\000\000\000C\000\000\000\n\b\0062\006\b\000\020\0000\000\016\001\203\255\315\371\000\000\000"
  "\036\n\016standalone-0-"
  "3\020\000\030\344\277\364\362\2062X\001\300\001\000\000\000\000\004\030\005@\000XXXXX");
}  // namespace pulsar
}  // namespace protocols
}  // namespace stirling
}  // namespace px
