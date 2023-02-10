import logging
import ssl
import os
from http.server import HTTPServer, BaseHTTPRequestHandler

logging.basicConfig(level=logging.INFO,
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
        for _ in range(0, 1024):
            self.wfile.write(bytes(payload, 'utf-8'))

httpd = HTTPServer(('localhost', 443), MyRequestHandler)

httpd.socket = ssl.wrap_socket(httpd.socket,
                               keyfile="/etc/ssl/server.key",
                               certfile='/etc/ssl/server.crt', server_side=True)

httpd.serve_forever()
