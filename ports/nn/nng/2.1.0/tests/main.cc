#include <nng/nng.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

void check(bool ok, const char *what, int line) {
  if (!ok) {
    std::fprintf(stderr, "main.cc:%d: check failed: %s\n", line, what);
    std::exit(1);
  }
}

void pass(int rv, const char *what, int line) {
  if (rv != 0) {
    std::fprintf(stderr, "main.cc:%d: %s: %s\n", line, what,
                 nng_strerror(static_cast<nng_err>(rv)));
    std::exit(1);
  }
}

#define CHECK(cond) check((cond), #cond, __LINE__)
#define PASS(expr) pass(static_cast<int>(expr), #expr, __LINE__)

void set_timeouts(nng_socket s) {
  PASS(nng_socket_set_ms(s, NNG_OPT_RECVTIMEO, 5000));
  PASS(nng_socket_set_ms(s, NNG_OPT_SENDTIMEO, 5000));
}

// Messages carry their bytes without a terminator.
void send_str(nng_socket s, const std::string &msg) {
  PASS(nng_send(s, msg.data(), msg.size(), 0));
}

std::string recv_str(nng_socket s) {
  char buf[64];
  std::size_t size = sizeof(buf);
  PASS(nng_recv(s, buf, &size, 0));
  return std::string(buf, size);
}

void test_version_and_errors() {
  CHECK(std::string(nng_version()) == "2.1.0");
  CHECK(std::string(nng_strerror(NNG_ETIMEDOUT)) == "Timed out");
  CHECK(std::string(nng_strerror(NNG_ECONNREFUSED)) == "Connection refused");
}

void test_url_parse() {
  nng_url *url = nullptr;
  PASS(nng_url_parse(&url, "tcp://[::1]:5555"));
  CHECK(std::string(nng_url_scheme(url)) == "tcp");
  CHECK(std::string(nng_url_hostname(url)) == "::1");
  CHECK(nng_url_port(url) == 5555);
  CHECK(nng_url_userinfo(url) == nullptr);
  nng_url_free(url);

  PASS(nng_url_parse(&url, "wss://user:secret@Example.ORG/feed?x=y#z"));
  CHECK(std::string(nng_url_userinfo(url)) == "user:secret");
  CHECK(std::string(nng_url_hostname(url)) == "example.org");
  CHECK(nng_url_port(url) == 443);
  CHECK(std::string(nng_url_path(url)) == "/feed");
  CHECK(std::string(nng_url_query(url)) == "x=y");
  CHECK(std::string(nng_url_fragment(url)) == "z");
  nng_url_free(url);
}

void test_message_encoding() {
  nng_msg *msg = nullptr;
  std::uint32_t value = 0;
  PASS(nng_msg_alloc(&msg, 0));
  PASS(nng_msg_append(msg, "ab", 2));
  PASS(nng_msg_append_u32(msg, 0xdeadbeefu));
  CHECK(nng_msg_len(msg) == 6);
  const auto *body = static_cast<const unsigned char *>(nng_msg_body(msg));
  CHECK(body[0] == 'a' && body[1] == 'b');
  CHECK(body[2] == 0xde && body[3] == 0xad && body[4] == 0xbe &&
        body[5] == 0xef);
  PASS(nng_msg_chop_u32(msg, &value));
  CHECK(value == 0xdeadbeefu);
  CHECK(nng_msg_len(msg) == 2);
  nng_msg_free(msg);
}

void test_pair1_inproc() {
  nng_socket a, b;
  PASS(nng_pair1_open(&a));
  PASS(nng_pair1_open(&b));
  set_timeouts(a);
  set_timeouts(b);
  PASS(nng_listen(a, "inproc://cabin-nng-cc-pair", nullptr, 0));
  PASS(nng_dial(b, "inproc://cabin-nng-cc-pair", nullptr, 0));
  send_str(b, "ping");
  CHECK(recv_str(a) == "ping");
  send_str(a, "pong");
  CHECK(recv_str(b) == "pong");
  PASS(nng_socket_close(a));
  PASS(nng_socket_close(b));
}

void test_reqrep_inproc() {
  nng_socket rep, req;
  PASS(nng_rep0_open(&rep));
  PASS(nng_req0_open(&req));
  set_timeouts(rep);
  set_timeouts(req);
  PASS(nng_listen(rep, "inproc://cabin-nng-cc-reqrep", nullptr, 0));
  PASS(nng_dial(req, "inproc://cabin-nng-cc-reqrep", nullptr, 0));
  send_str(req, "question");
  CHECK(recv_str(rep) == "question");
  send_str(rep, "answer");
  CHECK(recv_str(req) == "answer");
  PASS(nng_socket_close(req));
  PASS(nng_socket_close(rep));
}

// The socket transport over a connected descriptor pair, which nng creates
// only on POSIX systems.  (No TCP, IPC or websocket listeners: sandboxed
// test runs may forbid binding sockets.)
void test_socket_transport() {
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
  CHECK(recv_str(b) == "over a socket pair");
  send_str(b, "reply");
  CHECK(recv_str(a) == "reply");
  PASS(nng_socket_close(a));
  PASS(nng_socket_close(b));
#endif
}

void test_recv_without_peer() {
  nng_socket s;
  char buf[8];
  std::size_t size = sizeof(buf);
  PASS(nng_pair1_open(&s));
  PASS(nng_socket_set_ms(s, NNG_OPT_RECVTIMEO, 50));
  CHECK(nng_recv(s, buf, &size, 0) == NNG_ETIMEDOUT);
  CHECK(nng_recv(s, buf, &size, NNG_FLAG_NONBLOCK) == NNG_EAGAIN);
  PASS(nng_socket_close(s));
}

}  // namespace

int main() {
  PASS(nng_init(nullptr));
  test_version_and_errors();
  test_url_parse();
  test_message_encoding();
  test_pair1_inproc();
  test_reqrep_inproc();
  test_socket_transport();
  test_recv_without_peer();
  nng_fini();
  std::puts("nng C++ test passed");
  return 0;
}
