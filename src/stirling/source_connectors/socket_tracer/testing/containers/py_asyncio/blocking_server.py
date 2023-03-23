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

import logging
import ssl
import os
from http.server import HTTPServer, BaseHTTPRequestHandler

logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s %(levelname)s %(message)s',
    handlers=[logging.StreamHandler()])

pid = os.getpid()
logging.info(f"pid={pid}")


class MyRequestHandler(BaseHTTPRequestHandler):
    def do_GET(self):
        user_agent = self.headers['User-Agent']
        payload = user_agent * 1024

        self.send_response(200)
        self.send_header("Content-type", "text/plain")
        self.end_headers()

        # TODO(ddelnano): Allow the write iterations to be configured with a cli
        # argument to make testing different interations easier. For example, if
        # 10 KiB or 1 MiB payloads were desired it would be convenient to configure
        # this at runtime.
        for _ in range(0, 5):
            self.wfile.write(bytes(payload, 'utf-8'))


httpd = HTTPServer(('localhost', 443), MyRequestHandler)

httpd.socket = ssl.wrap_socket(httpd.socket,
                               keyfile="/etc/ssl/server.key",
                               certfile='/etc/ssl/server.crt', server_side=True)

httpd.serve_forever()
