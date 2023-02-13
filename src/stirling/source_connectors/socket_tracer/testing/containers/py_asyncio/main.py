import asyncio
import logging
import os

import tornado.httpserver
import tornado.ioloop
import tornado.web

logging.basicConfig(level=logging.INFO,
  format='%(asctime)s %(levelname)s %(message)s',
  handlers=[logging.StreamHandler()])

response = """
<!DOCTYPE html>
<html>
<head>
<title>Welcome to nginx!</title>
<style>
    body {
        width: 35em;
        margin: 0 auto;
        font-family: Tahoma, Verdana, Arial, sans-serif;
    }
</style>
</head>
<body>
<h1>Welcome to nginx!</h1>
<p>If you see this page, the nginx web server is successfully installed and
working. Further configuration is required.</p>

<p>For online documentation and support please refer to
<a href="http://nginx.org/">nginx.org</a>.<br/>
Commercial support is available at
<a href... [TRUNCATED])""";

class getOK(tornado.web.RequestHandler):
    def get(self):
        user_agent = self.request.headers['User-Agent']
        payload = user_agent * 1024

        # TODO(ddelnano): Allow the write times to be configured with a cli
        # argument to make testing different interations easier.
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
