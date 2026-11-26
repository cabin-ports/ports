#include <rabbitmq-c/amqp.h>
#include <rabbitmq-c/ssl_socket.h>

#include <openssl/crypto.h>

#include <stdio.h>
#include <string.h>

int main(void) {
  amqp_connection_state_t conn = amqp_new_connection();
  if (conn == NULL || amqp_ssl_socket_new(conn) == NULL ||
      strcmp(amqp_version(), "0.18.1") != 0 ||
      OPENSSL_version_major() != 4) {
    return 1;
  }
  amqp_destroy_connection(conn);
  puts("rabbitmq-c vendored ok");
  return 0;
}
