#include <rabbitmq-c/amqp.h>
#include <rabbitmq-c/ssl_socket.h>
#include <rabbitmq-c/tcp_socket.h>

#include <stdio.h>
#include <string.h>

#define CHECK(cond)                                         \
  do {                                                      \
    if (!(cond)) {                                          \
      fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #cond); \
      return 1;                                             \
    }                                                       \
  } while (0)

static int parse(const char *url, struct amqp_connection_info *ci) {
  static char buf[128];
  strcpy(buf, url);
  return amqp_parse_url(buf, ci);
}

int main(void) {
  struct amqp_connection_info ci;

  CHECK(strcmp(amqp_version(), "0.18.1") == 0);
  CHECK(amqp_version_number() == 0x00120101u);

  CHECK(parse("amqp://guest:secret@localhost:5673/vh", &ci) == AMQP_STATUS_OK);
  CHECK(strcmp(ci.user, "guest") == 0 && strcmp(ci.password, "secret") == 0);
  CHECK(strcmp(ci.host, "localhost") == 0 && ci.port == 5673);
  CHECK(strcmp(ci.vhost, "vh") == 0 && !ci.ssl);

  CHECK(parse("amqps://h", &ci) == AMQP_STATUS_OK);
  CHECK(strcmp(ci.host, "h") == 0 && ci.port == 5671 && ci.ssl);
  CHECK(strcmp(ci.vhost, "/") == 0 && strcmp(ci.user, "guest") == 0);

  CHECK(parse("amqp://u@[::1]/a%2Fb", &ci) == AMQP_STATUS_OK);
  CHECK(strcmp(ci.user, "u") == 0 && strcmp(ci.password, "guest") == 0);
  CHECK(strcmp(ci.host, "::1") == 0 && ci.port == 5672);
  CHECK(strcmp(ci.vhost, "a/b") == 0);

  CHECK(parse("amqp://h:99999", &ci) == AMQP_STATUS_BAD_URL);
  CHECK(parse("amqp://h:x", &ci) == AMQP_STATUS_BAD_URL);
  CHECK(parse("amqp://h/a/b", &ci) == AMQP_STATUS_BAD_URL);
  CHECK(parse("http://h", &ci) == AMQP_STATUS_BAD_URL);

  CHECK(strcmp(amqp_error_string2(AMQP_STATUS_OK),
               "operation completed successfully") == 0);
  CHECK(strcmp(amqp_error_string2(AMQP_STATUS_BAD_URL),
               "could not parse AMQP URL") == 0);
  CHECK(strcmp(amqp_error_string2(AMQP_STATUS_SSL_ERROR),
               "a SSL error occurred") == 0);
  CHECK(strcmp(amqp_error_string2(-0x7fff), "(unknown error)") == 0);

  {
    amqp_bytes_t a = amqp_cstring_bytes("abc");
    amqp_bytes_t b = amqp_bytes_malloc_dup(a);
    CHECK(a.len == 3 && b.len == 3 && b.bytes != a.bytes);
    CHECK(memcmp(b.bytes, "abc", 3) == 0);
    amqp_bytes_free(b);
  }

  {
    /* Encoded size: 4-byte length, then entries of 7 ("n": I32), 12 ("s":
       "hello") and 4 ("b": true) bytes. */
    amqp_table_entry_t entries[3];
    amqp_table_t table, decoded, cloned;
    amqp_pool_t pool;
    char buf[64];
    amqp_bytes_t enc;
    size_t offset = 0;

    entries[0].key = amqp_cstring_bytes("n");
    entries[0].value.kind = AMQP_FIELD_KIND_I32;
    entries[0].value.value.i32 = -7;
    entries[1].key = amqp_cstring_bytes("s");
    entries[1].value.kind = AMQP_FIELD_KIND_UTF8;
    entries[1].value.value.bytes = amqp_cstring_bytes("hello");
    entries[2].key = amqp_cstring_bytes("b");
    entries[2].value.kind = AMQP_FIELD_KIND_BOOLEAN;
    entries[2].value.value.boolean = 1;
    table.num_entries = 3;
    table.entries = entries;

    enc.bytes = buf;
    enc.len = sizeof buf;
    CHECK(amqp_encode_table(enc, &table, &offset) == AMQP_STATUS_OK);
    CHECK(offset == 27);

    enc.len = 10;
    offset = 0;
    CHECK(amqp_encode_table(enc, &table, &offset) == AMQP_STATUS_TABLE_TOO_BIG);

    init_amqp_pool(&pool, 4096);
    enc.len = sizeof buf;
    offset = 0;
    CHECK(amqp_decode_table(enc, &pool, &decoded, &offset) == AMQP_STATUS_OK);
    CHECK(offset == 27 && decoded.num_entries == 3);
    CHECK(decoded.entries[0].value.kind == AMQP_FIELD_KIND_I32);
    CHECK(decoded.entries[0].value.value.i32 == -7);
    CHECK(decoded.entries[1].value.value.bytes.len == 5);
    CHECK(memcmp(decoded.entries[1].value.value.bytes.bytes, "hello", 5) == 0);
    CHECK(decoded.entries[2].value.value.boolean);

    CHECK(amqp_table_clone(&decoded, &cloned, &pool) == AMQP_STATUS_OK);
    CHECK(cloned.num_entries == 3 && cloned.entries != decoded.entries);
    CHECK(cloned.entries[0].value.value.i32 == -7);
    empty_amqp_pool(&pool);
  }

  {
    amqp_connection_state_t conn = amqp_new_connection();
    amqp_socket_t *tcp;
    CHECK(conn != NULL);
    CHECK(amqp_get_channel_max(conn) == 0 && amqp_get_heartbeat(conn) == 0);
    tcp = amqp_tcp_socket_new(conn);
    CHECK(tcp != NULL && amqp_get_socket(conn) == tcp);
    CHECK(amqp_destroy_connection(conn) == AMQP_STATUS_OK);
  }

  {
    amqp_connection_state_t conn = amqp_new_connection();
    amqp_socket_t *ssl;
    CHECK(conn != NULL);
    ssl = amqp_ssl_socket_new(conn);
    CHECK(ssl != NULL && amqp_get_socket(conn) == ssl);
    CHECK(amqp_ssl_socket_get_context(ssl) != NULL);
    CHECK(amqp_ssl_socket_set_cacert(ssl, "/nonexistent/ca.pem") ==
          AMQP_STATUS_SSL_ERROR);
    CHECK(amqp_destroy_connection(conn) == AMQP_STATUS_OK);
  }

  puts("rabbitmq-c ok");
  return 0;
}
