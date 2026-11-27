// C++ consumer: parses AMQP addresses and tables, then drives a client
// Connection through the 0-9-1 handshake with frames built here, checking the
// bytes the library writes.
#include <amqpcpp.h>

#include <cstdint>
#include <cstdio>
#include <string>

namespace {

int failures = 0;

#define CHECK(cond)                                                   \
  do {                                                                \
    if (!(cond)) {                                                    \
      std::printf("main.cc:%d: check failed: %s\n", __LINE__, #cond); \
      ++failures;                                                     \
    }                                                                 \
  } while (0)

class Handler : public AMQP::ConnectionHandler {
 public:
  std::string sent;
  std::string server_product;
  bool ready = false;
  bool errored = false;

  void onProperties(AMQP::Connection *, const AMQP::Table &server,
                    AMQP::Table &client) override {
    // Unset, upstream reads /proc/self/exe, which is absent on macOS.
    client["product"] = "test";
    client["connection_name"] = "test";
    server_product = static_cast<const std::string &>(server.get("product"));
  }
  void onData(AMQP::Connection *, const char *buffer, size_t size) override {
    sent.append(buffer, size);
  }
  void onReady(AMQP::Connection *) override { ready = true; }
  void onError(AMQP::Connection *, const char *) override { errored = true; }
};

// One frame: type, channel, size, payload, frame-end 0xCE.
std::string frame(char type, char channel, const std::string &payload) {
  std::string f(1, type);
  f += std::string("\0", 1) + channel;
  uint32_t n = static_cast<uint32_t>(payload.size());
  for (int shift = 24; shift >= 0; shift -= 8) f += static_cast<char>(n >> shift);
  return f + payload + "\xCE";
}

std::string bytes(std::initializer_list<int> v) {
  std::string s;
  for (int b : v) s += static_cast<char>(b);
  return s;
}

}  // namespace

int main() {
  AMQP::Address a("amqp://guest:secret@broker.example:5673/vh");
  CHECK(a.hostname() == "broker.example");
  CHECK(a.port() == 5673);
  CHECK(a.vhost() == "vh");
  CHECK(a.login().user() == "guest");
  CHECK(a.login().password() == "secret");
  CHECK(!a.secure());

  AMQP::Address s("amqps://h");
  CHECK(s.secure());
  CHECK(s.port() == 5671);
  CHECK(s.vhost() == "/");

  AMQP::Table t;
  t.set("k", std::string("v"));
  t.set("n", 42);
  CHECK(t.contains("k"));
  CHECK(!t.contains("missing"));
  CHECK(static_cast<const std::string &>(t.get("k")) == "v");
  CHECK(static_cast<int32_t>(t.get("n")) == 42);

  Handler h;
  AMQP::Connection conn(&h, AMQP::Login("guest", "guest"), "/");
  CHECK(h.sent == "AMQP" + bytes({0, 0, 9, 1}));
  h.sent.clear();

  // Connection.Start: version 0-9, server-properties {product: "Test"},
  // mechanisms "PLAIN", locales "en_US".
  std::string start = bytes({0, 10, 0, 10, 0, 9}) +
                      bytes({0, 0, 0, 17, 7}) + "product" +
                      bytes({'S', 0, 0, 0, 4}) + "Test" +
                      bytes({0, 0, 0, 5}) + "PLAIN" +
                      bytes({0, 0, 0, 5}) + "en_US";
  std::string in = frame(1, 0, start);
  CHECK(conn.parse(in.data(), in.size()) == in.size());
  CHECK(h.server_product == "Test");
  CHECK(h.sent.find("PLAIN") != std::string::npos);
  // The client properties carry the library version (the VERSION define).
  CHECK(h.sent.find("4.3.27") != std::string::npos);
  CHECK(h.sent.find(std::string("\0guest\0guest", 12)) != std::string::npos);
  h.sent.clear();

  // Connection.Tune: channel-max 2047, frame-max 131072, heartbeat 60.
  in = frame(1, 0, bytes({0, 10, 0, 30, 0x07, 0xFF, 0, 2, 0, 0, 0, 60}));
  CHECK(conn.parse(in.data(), in.size()) == in.size());
  CHECK(!h.ready);
  CHECK(conn.maxFrame() == 131072);
  // Connection.TuneOk (heartbeats declined) then Connection.Open for "/".
  CHECK(h.sent == frame(1, 0, bytes({0, 10, 0, 31, 0x07, 0xFF, 0, 2, 0, 0, 0, 0})) +
                      frame(1, 0, bytes({0, 10, 0, 40, 1}) + "/" + bytes({0, 0})));
  h.sent.clear();

  // Connection.OpenOk makes the connection ready.
  in = frame(1, 0, bytes({0, 10, 0, 41, 0}));
  CHECK(conn.parse(in.data(), in.size()) == in.size());
  CHECK(h.ready);
  CHECK(conn.ready());
  CHECK(!h.errored);

  // Opening a channel writes Channel.Open on channel 1.
  AMQP::Channel channel(&conn);
  CHECK(h.sent == frame(1, 1, bytes({0, 20, 0, 10, 0})));

  if (failures == 0) std::printf("amqp-cpp: all checks passed\n");
  return failures == 0 ? 0 : 1;
}
