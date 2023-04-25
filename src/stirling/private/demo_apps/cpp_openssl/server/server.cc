/*
 * Copyright © 2018- Pixie Labs Inc.
 * Copyright © 2020- New Relic, Inc.
 * All Rights Reserved.
 *
 * NOTICE:  All information contained herein is, and remains
 * the property of New Relic Inc. and its suppliers,
 * if any.  The intellectual and technical concepts contained
 * herein are proprietary to Pixie Labs Inc. and its suppliers and
 * may be covered by U.S. and Foreign Patents, patents in process,
 * and are protected by trade secret or copyright law. Dissemination
 * of this information or reproduction of this material is strictly
 * forbidden unless prior written permission is obtained from
 * New Relic, Inc.
 *
 * SPDX-License-Identifier: Proprietary
 */

// Reference: https://wiki.openssl.org/index.php/Simple_TLS_Server
#include <arpa/inet.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <iostream>
#include "src/common/base/base.h"

int create_socket(int port) {
  int s;
  struct sockaddr_in addr;

  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = htonl(INADDR_ANY);

  s = socket(AF_INET, SOCK_STREAM, 0);
  if (s < 0) {
    perror("Unable to create socket");
    exit(EXIT_FAILURE);
  }

  if (bind(s, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
    perror("Unable to bind");
    exit(EXIT_FAILURE);
  }

  if (listen(s, 1) < 0) {
    perror("Unable to listen");
    exit(EXIT_FAILURE);
  }

  return s;
}

void init_openssl() {
  SSL_load_error_strings();
  OpenSSL_add_ssl_algorithms();
}

void cleanup_openssl() { EVP_cleanup(); }

SSL_CTX* create_context() {
  const SSL_METHOD* method;
  SSL_CTX* ctx;

  method = SSLv23_server_method();

  ctx = SSL_CTX_new(method);
  if (!ctx) {
    perror("Unable to create SSL context");
    ERR_print_errors_fp(stderr);
    exit(EXIT_FAILURE);
  }

  return ctx;
}

void configure_context(SSL_CTX* ctx) {
  SSL_CTX_set_ecdh_auto(ctx, 1);

  SSL_CTX_set_mode(ctx, SSL_MODE_ASYNC);

  /* Set the key and cert */
  if (SSL_CTX_use_certificate_file(
          ctx, "src/stirling/private/demo_apps/cpp_openssl/server/cpp-tls-server-cert.pem",
          SSL_FILETYPE_PEM) <= 0) {
    ERR_print_errors_fp(stderr);
    exit(EXIT_FAILURE);
  }

  if (SSL_CTX_use_PrivateKey_file(
          ctx, "src/stirling/private/demo_apps/cpp_openssl/server/cpp-tls-server.pem",
          SSL_FILETYPE_PEM) <= 0) {
    ERR_print_errors_fp(stderr);
    exit(EXIT_FAILURE);
  }
}

int main() {
  int sock;
  SSL_CTX* ctx;

  init_openssl();
  ctx = create_context();
  configure_context(ctx);
  sock = create_socket(4433);

  SSL* ssl;
  struct sockaddr_in addr;
  uint len = sizeof(addr);

  // Accept TCP connection
  while (true) {
    std::cout << "Calling accept" << std::endl;
    int client = accept(sock, (struct sockaddr*)&addr, &len);
    if (client <= 0) {
      perror("Unable to accept");
      exit(EXIT_FAILURE);
    } else {
      std::cout << "Client connected." << std::endl;
    }

    // Establish TLS connection
    ssl = SSL_new(ctx);
    SSL_set_fd(ssl, client);

    if (SSL_accept(ssl) <= 0) {
      ERR_print_errors_fp(stderr);
    } else {
      std::cout << "TLS connection established." << std::endl;
    }

    char buf[1024] = {};
    const std::string_view r2 = "HTTP/1.1 200 OK\r\n\r\n";
    std::string response;
    usleep(1000000);
    SSL_read(ssl, buf, 1024);
    std::cout << buf << std::endl;
    response = absl::StrCat(r2);
    SSL_write(ssl, response.c_str(), response.size());

    SSL_shutdown(ssl);
    SSL_free(ssl);
    close(client);
  }
  close(sock);
  SSL_CTX_free(ctx);
  cleanup_openssl();
}
