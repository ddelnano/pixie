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

import asyncio
import logging
import os

import tornado.httpserver
import tornado.ioloop
import tornado.web

logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s %(levelname)s %(message)s',
    handlers=[logging.StreamHandler()])


class getOK(tornado.web.RequestHandler):
    def get(self):
        user_agent = self.request.headers['User-Agent']
        payload = user_agent * 1024

        # TODO(ddelnano): Allow the write iterations to be configured with a cli
        # argument to make testing different interations easier. For example, if
        # 10 KiB or 1 MiB payloads were desired it would be convenient to configure
        # this at runtime.
        for _ in range(0, 5):
            self.write(payload)


async def main():
    pid = os.getpid()
    logging.info(f"pid={pid}")
    application = tornado.web.Application([
        (r'/.*', getOK),
    ])
    http_server = tornado.httpserver.HTTPServer(application, ssl_options={
        "certfile": "/etc/ssl/server.crt",
        "keyfile": "/etc/ssl/server.key",
    })
    http_server.listen(443)
    await asyncio.Event().wait()

if __name__ == '__main__':
    asyncio.run(main())
