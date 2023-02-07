import logging
import ssl
import os
from http.server import HTTPServer, BaseHTTPRequestHandler

logging.basicConfig(level=logging.INFO,
  format='%(asctime)s %(levelname)s %(message)s',
  handlers=[logging.StreamHandler()])

pid = os.getpid()
logging.info(f"pid={pid}")

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

class MyRequestHandler(BaseHTTPRequestHandler):
    def do_GET(self):
        self.send_response(200)
        self.send_header("Content-type", "text/html")
        self.end_headers()
        self.wfile.write(bytes(response, 'utf-8'))
        # self.wfile.close()

httpd = HTTPServer(('localhost', 443), MyRequestHandler)

httpd.socket = ssl.wrap_socket(httpd.socket,
                               keyfile="/etc/ssl/server.key",
                               certfile='/etc/ssl/server.crt', server_side=True)

httpd.serve_forever()
