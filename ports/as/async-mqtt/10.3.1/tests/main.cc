#include <async_mqtt/all.hpp>
#include <async_mqtt/asio_bind/predefined_layer/ws.hpp>

#include <algorithm>
#include <cstdio>
#include <iterator>
#include <sstream>
#include <string>
#include <variant>

namespace am = async_mqtt;

static int failures = 0;

#define CHECK(cond)                                                    \
  do {                                                                 \
    if (!(cond)) {                                                     \
      std::fprintf(stderr, "async-mqtt: %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      ++failures;                                                      \
    }                                                                  \
  } while (0)

int main() {
  // PUBLISH serialization and parsing round trip.
  {
    auto p = am::v3_1_1::publish_packet{
        0x1234, "topic1", "payload1",
        am::qos::exactly_once | am::pub::retain::yes | am::pub::dup::yes};
    CHECK(p.packet_id() == 0x1234);
    CHECK(p.topic() == "topic1");
    CHECK(p.opts().get_qos() == am::qos::exactly_once);
    CHECK(p.opts().get_dup() == am::pub::dup::yes);
    p.set_dup(false);
    CHECK(p.opts().get_dup() == am::pub::dup::no);

    const char expected[] = {0x35, 0x12, 0x00, 0x06, 't', 'o', 'p', 'i', 'c', '1',
                             0x12, 0x34, 'p', 'a', 'y', 'l', 'o', 'a', 'd', '1'};
    auto cbs = p.const_buffer_sequence();
    CHECK(cbs.size() == p.num_of_const_buffer_sequence());
    auto [b, e] = am::make_packet_range(cbs);
    CHECK(std::equal(b, e, std::begin(expected), std::end(expected)));

    am::error_code ec;
    auto v = am::buffer_to_packet_variant(
        am::buffer{expected, sizeof(expected)}, am::protocol_version::v3_1_1, ec);
    CHECK(!ec);
    CHECK(v.has_value());
    if (v) {
      v->visit(am::overload{
          [](am::v3_1_1::publish_packet const& q) {
            CHECK(q.packet_id() == 0x1234);
            CHECK(q.topic() == "topic1");
            CHECK(q.opts().get_qos() == am::qos::exactly_once);
            CHECK(q.opts().get_retain() == am::pub::retain::yes);
            CHECK(q.opts().get_dup() == am::pub::dup::no);
          },
          [](auto const&) { CHECK(false); }});
    }
  }

  // A malformed packet is rejected with an error.
  {
    const char bad[] = {0x35, 0x12, 0x00};
    am::error_code ec;
    (void)am::buffer_to_packet_variant(
        am::buffer{bad, sizeof(bad)}, am::protocol_version::v3_1_1, ec);
    CHECK(ec);
  }

  // Sans-I/O client connection: CONNECT out, CONNACK in, then PUBLISH QoS1.
  {
    am::rv_connection<am::role::client> c{am::protocol_version::v5};
    CHECK(c.get_connection_status() == am::connection_status::disconnected);

    auto events = c.send(am::v5::connect_packet{
        true, 0x1234, "cid1", std::nullopt, std::nullopt, std::nullopt,
        am::properties{am::property::session_expiry_interval(
            am::session_never_expire)}});
    CHECK(c.get_connection_status() == am::connection_status::connecting);
    CHECK(events.size() == 2);
    CHECK(std::holds_alternative<am::event::send>(events[0]));
    if (auto* t = std::get_if<am::event::timer>(&events[1])) {
      CHECK(t->get_kind() == am::timer_kind::pingreq_send);
      CHECK(t->get_ms() == std::chrono::seconds{0x1234});
    } else {
      CHECK(false);
    }

    std::istringstream is{am::to_string(
        am::v5::connack_packet{false, am::connect_reason_code::success}
            .const_buffer_sequence())};
    events = c.recv(is);
    CHECK(events.size() == 1);
    CHECK(std::holds_alternative<am::event::packet_received>(events[0]));
    CHECK(c.get_connection_status() == am::connection_status::connected);

    auto pid = c.acquire_unique_packet_id();
    CHECK(pid.has_value());
    auto publish = am::v5::publish_packet{*pid, "topic1", "payload1",
                                          am::qos::at_least_once};
    events = c.send(publish);
    CHECK(!events.empty());
    CHECK(std::holds_alternative<am::event::send>(events[0]));

    // The unacknowledged QoS1 PUBLISH is stored and resent with DUP on reconnect.
    c.notify_closed();
    CHECK(c.get_connection_status() == am::connection_status::disconnected);
    auto stored = c.get_stored_packets();
    CHECK(stored.size() == 1);
    auto dup = publish;
    dup.set_dup(true);
    stored[0].visit(am::overload{
        [&](am::v5::publish_packet const& sp) { CHECK(sp == dup); },
        [](auto const&) { CHECK(false); }});
  }

  // Asio-side layers compile and construct without touching the network.
  {
    boost::asio::io_context ioc;
    am::endpoint<am::role::client, am::protocol::mqtt> ep{
        am::protocol_version::v3_1_1, ioc.get_executor()};
    CHECK(ep.get_executor() == ioc.get_executor());
    CHECK(sizeof(am::protocol::ws) > 0);
  }

  return failures == 0 ? 0 : 1;
}
