// Builds the transport this target adds and constructs a client on it; nothing connects.
#include <mqtt_client_cpp.hpp>

#include <cstdio>

int main() {
  namespace as = boost::asio;
  as::io_context ioc;
  auto c = MQTT_NS::make_tls_sync_client(ioc, "localhost", 8883);
  c->set_client_id("cid");
  if (c->get_client_id() != "cid" || c->connected() || c->get_ssl_context().native_handle() == nullptr) {
    std::fputs("FAIL: tls client\n", stderr);
    return 1;
  }
  std::puts("mqtt-cpp: tls client constructed");
  return 0;
}
