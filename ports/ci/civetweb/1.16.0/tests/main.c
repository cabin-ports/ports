#include <civetweb.h>

#include <stdio.h>
#include <string.h>

static int failures = 0;

static void check(int ok, const char *what) {
  if (!ok) {
    fprintf(stderr, "FAIL: %s\n", what);
    failures++;
  }
}

static int hello(struct mg_connection *conn, void *cbdata) {
  const struct mg_request_info *ri = mg_get_request_info(conn);
  mg_printf(conn,
            "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n"
            "Content-Length: %d\r\nConnection: close\r\n\r\n%s?%s",
            (int)(strlen((const char *)cbdata) + 1 +
                  (ri->query_string ? strlen(ri->query_string) : 0)),
            (const char *)cbdata, ri->query_string ? ri->query_string : "");
  return 200;
}

static int socket_denied;

static int on_log(const struct mg_connection *c, const char *msg) {
  (void)c;
  if (strstr(msg, "cannot create socket") || strstr(msg, "cannot bind") ||
      strstr(msg, "cannot listen")) {
    socket_denied = 1;
  }
  fprintf(stderr, "civetweb: %s\n", msg);
  return 1;
}

int main(void) {
  struct mg_callbacks callbacks;
  char buf[128];
  size_t len;
  char ebuf[256] = "";
  const char *options[] = {"listening_ports", "127.0.0.1:0", "num_threads", "2",
                           NULL};
  struct mg_server_port ports[4];
  struct mg_context *ctx;
  struct mg_connection *conn;

  mg_init_library(0);

  check(strcmp(mg_version(), "1.16") == 0, "version");
  check(mg_check_feature(8) != 0, "IPv6 compiled in");
  check(mg_check_feature(16) != 0, "websocket compiled in");

  check(mg_url_decode("a%20b+c%2F", 10, buf, sizeof buf, 0) == 6 &&
            strcmp(buf, "a b+c/") == 0,
        "url_decode");
  check(mg_url_decode("a%20b+c%2F", 10, buf, sizeof buf, 1) == 6 &&
            strcmp(buf, "a b c/") == 0,
        "url_decode form");
  check(mg_url_encode("a b/c&d", buf, sizeof buf) == 13 &&
            strcmp(buf, "a%20b%2fc%26d") == 0,
        "url_encode");
  len = sizeof buf;
  check(mg_base64_encode((const unsigned char *)"foobar", 6, buf, &len) == -1 &&
            strcmp(buf, "Zm9vYmFy") == 0,
        "base64_encode");
  check(strcmp(mg_md5(buf, "a", "bc", NULL), "900150983cd24fb0d6963f7d28e17f72") == 0,
        "md5");

  memset(&callbacks, 0, sizeof callbacks);
  callbacks.log_message = on_log;
  ctx = mg_start(&callbacks, NULL, options);
  if (ctx == NULL && socket_denied) {
    /* The check sandbox has no loopback; only the pure functions run there. */
    puts("loopback unavailable: server checks skipped");
    mg_exit_library();
    return failures != 0;
  }
  check(ctx != NULL, "mg_start");
  if (ctx == NULL) {
    mg_exit_library();
    return 1;
  }
  mg_set_request_handler(ctx, "/hello", hello, (void *)"world");
  check(mg_get_server_ports(ctx, 4, ports) >= 1 && ports[0].port > 0, "port");

  conn = mg_download("127.0.0.1", ports[0].port, 0, ebuf, sizeof ebuf,
                     "GET /hello?x=1 HTTP/1.0\r\nHost: 127.0.0.1\r\n\r\n");
  check(conn != NULL, "mg_download");
  if (conn != NULL) {
    int n;
    check(mg_get_response_info(conn)->status_code == 200, "status 200");
    n = mg_read(conn, buf, sizeof buf - 1);
    check(n == 9, "body length");
    if (n > 0) {
      buf[n] = '\0';
      check(strcmp(buf, "world?x=1") == 0, "body");
    }
    mg_close_connection(conn);
  }

  conn = mg_download("127.0.0.1", ports[0].port, 0, ebuf, sizeof ebuf,
                     "GET /missing HTTP/1.0\r\nHost: 127.0.0.1\r\n\r\n");
  check(conn != NULL, "mg_download missing");
  if (conn != NULL) {
    check(mg_get_response_info(conn)->status_code == 404, "status 404");
    mg_close_connection(conn);
  }

  mg_stop(ctx);
  mg_exit_library();
  return failures != 0;
}
