import tornado.httpserver
import tornado.ioloop
import tornado.web

class getOK(tornado.web.RequestHandler):
    def get(self):
        self.write("OK")

application = tornado.web.Application([
    (r'/', getOK),
])

if __name__ == '__main__':
    http_server = tornado.httpserver.HTTPServer(application, ssl_options={
        "certfile": "/etc/ssl/server.crt",
        "keyfile": "/etc/ssl/server.key",
    })
    http_server.listen(443)
    tornado.ioloop.IOLoop.instance().start()
