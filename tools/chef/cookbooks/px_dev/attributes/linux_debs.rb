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

if ! platform_family?('debian')
  return
end

default['clang-linters']['version']    = '21.1-pl1'
default['clang-linters']['deb']        =
  "https://github.com/ddelnano/dev-artifacts/releases/download/clang%2F#{default['clang-linters']['version']}/clang-linters-#{default['clang-linters']['version']}.deb"
default['clang-linters']['deb_sha256'] =
  '911dda4705a9c4ab43dee517244fc98825d7f94e8b993ecad03f18a6ceb2d835'
