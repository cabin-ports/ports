// Builds the transport this target adds and constructs a client on it; nothing connects.
#include <mqtt_client_cpp.hpp>

#include <cstdio>

int main() {
  namespace as = boost::asio;
  as::io_context ioc;
  auto c = MQTT_NS::make_sync_client_ws(ioc, "localhost", 80, "/mqtt");
  c->set_client_id("cid");
  if (c->get_client_id() != "cid" || c->connected()) {
    std::fputs("FAIL: ws client\n", stderr);
    return 1;
  }
  std::puts("mqtt-cpp: ws client constructed");
  return 0;
}
