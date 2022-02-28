import com.twitter.finagle.{Http, Service}
import com.twitter.finagle.http
import com.twitter.finagle.{Thrift, ThriftMux}
import com.twitter.finagle.ssl._
import com.twitter.finagle.ssl.server.SslServerConfiguration
import com.twitter.util.{Await, Future}
import java.net.{InetSocketAddress, InetAddress}
import com.twitter.finagle.thriftmux.thriftscala.TestService

import java.io.File;

object Server {
  def main(args: Array[String]): Unit = {
    // TODO(ddelnano): Add flag that determines if TLS should be used rather than
    // hardcoding TLS being enabled
    val keyCredentials = KeyCredentials.CertAndKey(
      new File("/etc/ssl/server.crt"),
      new File("/etc/ssl/server.key"),
    )
    val sslConfig = SslServerConfiguration(
      keyCredentials,
    )
    val server = ThriftMux.server
      .withTransport
      .tls(sslConfig)
      .serveIface(
        new InetSocketAddress(InetAddress.getLoopbackAddress, 8080),
        new TestService.MethodPerEndpoint {
          def query(x: String): Future[String] = Future.value(x + x)
          def question(y: String): Future[String] = Future.value(y + y)
          def inquiry(z: String): Future[String] = Future.value(z + z)
        },
      )
    Await.ready(server)
  }
}
