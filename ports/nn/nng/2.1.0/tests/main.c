#include <nng/nng.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond)                                                       \
  do {                                                                    \
    if (!(cond)) {                                                        \
      fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, \
              #cond);                                                     \
      exit(1);                                                            \
    }                                                                     \
  } while (0)

#define PASS(expr)                                                       \
  do {                                                                   \
    int rv_ = (int)(expr);                                               \
    if (rv_ != 0) {                                                      \
      fprintf(stderr, "%s:%d: %s: %s\n", __FILE__, __LINE__, #expr,      \
              nng_strerror((nng_err)rv_));                               \
      exit(1);                                                           \
    }                                                                    \
  } while (0)

static void set_timeouts(nng_socket s) {
  PASS(nng_socket_set_ms(s, NNG_OPT_RECVTIMEO, 5000));
  PASS(nng_socket_set_ms(s, NNG_OPT_SENDTIMEO, 5000));
}

static void send_str(nng_socket s, const char *msg) {
  PASS(nng_send(s, msg, strlen(msg) + 1, 0));
}

static void recv_str(nng_socket s, const char *want) {
  char buf[64];
  size_t size = sizeof(buf);
  PASS(nng_recv(s, buf, &size, 0));
  CHECK(size == strlen(want) + 1);
  CHECK(memcmp(buf, want, size) == 0);
}

static void test_version_and_errors(void) {
  CHECK(strcmp(nng_version(), "2.1.0") == 0);
  CHECK(strcmp(nng_strerror(NNG_ETIMEDOUT), "Timed out") == 0);
  CHECK(strcmp(nng_strerror(NNG_ECONNREFUSED), "Connection refused") == 0);
}

static void test_url_parse(void) {
  nng_url *url;
  PASS(nng_url_parse(&url, "ws://garrett@example.com:8080/chat?room=1#top"));
  CHECK(strcmp(nng_url_scheme(url), "ws") == 0);
  CHECK(strcmp(nng_url_userinfo(url), "garrett") == 0);
  CHECK(strcmp(nng_url_hostname(url), "example.com") == 0);
  CHECK(nng_url_port(url) == 8080);
  CHECK(strcmp(nng_url_path(url), "/chat") == 0);
  CHECK(strcmp(nng_url_query(url), "room=1") == 0);
  CHECK(strcmp(nng_url_fragment(url), "top") == 0);
  nng_url_free(url);

  PASS(nng_url_parse(&url, "http://example.com/index.html"));
  CHECK(nng_url_port(url) == 80);
  CHECK(nng_url_userinfo(url) == NULL);
  CHECK(nng_url_query(url) == NULL);
  nng_url_free(url);
}

static void test_message_encoding(void) {
  nng_msg *msg;
  uint32_t value = 0;
  const unsigned char *body;
  PASS(nng_msg_alloc(&msg, 0));
  PASS(nng_msg_append_u32(msg, 0x01020304u));
  PASS(nng_msg_append(msg, "xy", 2));
  CHECK(nng_msg_len(msg) == 6);
  body = (const unsigned char *)nng_msg_body(msg);
  CHECK(body[0] == 1 && body[1] == 2 && body[2] == 3 && body[3] == 4);
  CHECK(body[4] == 'x' && body[5] == 'y');
  PASS(nng_msg_trim_u32(msg, &value));
  CHECK(value == 0x01020304u);
  CHECK(nng_msg_len(msg) == 2);
  nng_msg_free(msg);
}

static void test_pair0_inproc(void) {
  nng_socket a, b;
  PASS(nng_pair0_open(&a));
  PASS(nng_pair0_open(&b));
  set_timeouts(a);
  set_timeouts(b);
  PASS(nng_listen(a, "inproc://cabin-nng-c-pair", NULL, 0));
  PASS(nng_dial(b, "inproc://cabin-nng-c-pair", NULL, 0));
  send_str(b, "ping");
  recv_str(a, "ping");
  send_str(a, "pong");
  recv_str(b, "pong");
  PASS(nng_socket_close(a));
  PASS(nng_socket_close(b));
}

static void test_reqrep_inproc(void) {
  nng_socket rep, req;
  PASS(nng_rep0_open(&rep));
  PASS(nng_req0_open(&req));
  set_timeouts(rep);
  set_timeouts(req);
  PASS(nng_listen(rep, "inproc://cabin-nng-c-reqrep", NULL, 0));
  PASS(nng_dial(req, "inproc://cabin-nng-c-reqrep", NULL, 0));
  send_str(req, "question");
  recv_str(rep, "question");
  send_str(rep, "answer");
  recv_str(req, "answer");
  PASS(nng_socket_close(req));
  PASS(nng_socket_close(rep));
}

// The socket transport over a connected descriptor pair, which nng creates
// only on POSIX systems.  (No TCP, IPC or websocket listeners: sandboxed
// test runs may forbid binding sockets.)
static void test_socket_transport(void) {
  int fds[2];
#ifdef _WIN32
  CHECK(nng_socket_pair(fds) == NNG_ENOTSUP);
#else
  nng_socket a, b;
  nng_listener la, lb;
  PASS(nng_socket_pair(fds));
  PASS(nng_pair1_open(&a));
  PASS(nng_pair1_open(&b));
  set_timeouts(a);
  set_timeouts(b);
  PASS(nng_listener_create(&la, a, "socket://"));
  PASS(nng_listener_start(la, 0));
  PASS(nng_listener_set_int(la, NNG_OPT_SOCKET_FD, fds[0]));
  PASS(nng_listener_create(&lb, b, "socket://"));
  PASS(nng_listener_start(lb, 0));
  PASS(nng_listener_set_int(lb, NNG_OPT_SOCKET_FD, fds[1]));
  send_str(a, "over a socket pair");
  recv_str(b, "over a socket pair");
  send_str(b, "reply");
  recv_str(a, "reply");
  PASS(nng_socket_close(a));
  PASS(nng_socket_close(b));
#endif
}

static void test_recv_without_peer(void) {
  nng_socket s;
  char buf[8];
  size_t size = sizeof(buf);
  PASS(nng_pair1_open(&s));
  PASS(nng_socket_set_ms(s, NNG_OPT_RECVTIMEO, 50));
  CHECK(nng_recv(s, buf, &size, 0) == NNG_ETIMEDOUT);
  CHECK(nng_recv(s, buf, &size, NNG_FLAG_NONBLOCK) == NNG_EAGAIN);
  PASS(nng_socket_close(s));
}

int main(void) {
  PASS(nng_init(NULL));
  test_version_and_errors();
  test_url_parse();
  test_message_encoding();
  test_pair0_inproc();
  test_reqrep_inproc();
  test_socket_transport();
  test_recv_without_peer();
  nng_fini();
  puts("nng C test passed");
  return 0;
}
