#include <mqtt_client_cpp.hpp>
#include <mqtt_server_cpp.hpp>
#include <mqtt/message.hpp>
#include <mqtt/property_parse.hpp>
#include <mqtt/utf8encoded_strings.hpp>
#include <mqtt/v5_message.hpp>

#include <cstdio>
#include <string>
#include <vector>

namespace as = boost::asio;
using namespace MQTT_NS::literals;

namespace {

int fail(const char* what) {
  std::fprintf(stderr, "mqtt-cpp: %s\n", what);
  return 1;
}

std::string str(MQTT_NS::string_view v) { return std::string(v.data(), v.size()); }

}  // namespace

int main() {
  // MQTT 3.1.1 CONNECT with will, user name and password, byte for byte as
  // section 3.1 lays it out.
  {
    MQTT_NS::optional<MQTT_NS::will> w = MQTT_NS::will(
        "wt"_mb, "wmsg"_mb, MQTT_NS::retain::no | MQTT_NS::qos::at_most_once);
    MQTT_NS::optional<MQTT_NS::buffer> user = "user"_mb;
    MQTT_NS::optional<MQTT_NS::buffer> password = "pw"_mb;
    auto m = MQTT_NS::connect_message(10, "cid"_mb, false, w, user, password);
    const std::string expected{
        0x10, 35,
        0, 4, 'M', 'Q', 'T', 'T',
        4,                             // protocol level 3.1.1
        static_cast<char>(0xC4),       // user, password, will; no clean session
        0, 10,                         // keep alive
        0, 3, 'c', 'i', 'd',
        0, 2, 'w', 't',
        0, 4, 'w', 'm', 's', 'g',
        0, 4, 'u', 's', 'e', 'r',
        0, 2, 'p', 'w'};
    if (m.continuous_buffer() != expected) return fail("CONNECT bytes");
  }

  // CONNACK with the session-present bit and return code 0.
  {
    auto m = MQTT_NS::connack_message(true, MQTT_NS::connect_return_code::accepted);
    if (m.continuous_buffer() != std::string{0x20, 2, 1, 0}) {
      return fail("CONNACK bytes");
    }
  }

  // PUBLISH QoS 1 with retain, then parsed back from its own bytes.
  {
    const std::string topic = "a/b";
    const std::string payload = "hi";
    auto m = MQTT_NS::publish_message(
        0x1234, as::buffer(topic.data(), topic.size()),
        as::buffer(payload.data(), payload.size()),
        MQTT_NS::qos::at_least_once | MQTT_NS::retain::yes);
    const std::string expected{0x33, 9, 0, 3, 'a', '/', 'b', 0x12, 0x34, 'h', 'i'};
    const std::string bytes = m.continuous_buffer();
    if (bytes != expected) return fail("PUBLISH bytes");

    auto parsed =
        MQTT_NS::publish_message(MQTT_NS::buffer(MQTT_NS::string_view(bytes)));
    auto body = parsed.payload_as_buffer();
    if (str(parsed.topic()) != topic || parsed.packet_id() != 0x1234 ||
        parsed.get_qos() != MQTT_NS::qos::at_least_once || !parsed.is_retain() ||
        parsed.is_dup() || str(body) != payload) {
      return fail("PUBLISH parse-back");
    }
  }

  // SUBSCRIBE and UNSUBSCRIBE.
  {
    const std::string filter = "tp";
    auto sub = MQTT_NS::subscribe_message(
        {{as::buffer(filter.data(), filter.size()), MQTT_NS::qos::at_least_once}}, 2);
    if (sub.continuous_buffer() !=
        std::string{static_cast<char>(0x82), 7, 0, 2, 0, 2, 't', 'p', 1}) {
      return fail("SUBSCRIBE bytes");
    }
    auto unsub = MQTT_NS::unsubscribe_message(
        {as::buffer(filter.data(), filter.size())}, 2);
    if (unsub.continuous_buffer() !=
        std::string{static_cast<char>(0xA2), 6, 0, 2, 0, 2, 't', 'p'}) {
      return fail("UNSUBSCRIBE bytes");
    }
  }

  // MQTT 5 PUBLISH with a four-byte and a user property: the property length
  // and each identifier are encoded as section 3.3.2.3 specifies, and the
  // properties parse back from the packet.
  {
    const std::string topic = "t";
    const std::string payload = "x";
    MQTT_NS::v5::properties props;
    props.emplace_back(MQTT_NS::v5::property::message_expiry_interval(300));
    props.emplace_back(MQTT_NS::v5::property::user_property("k"_mb, "v"_mb));
    auto m = MQTT_NS::v5::publish_message(
        0, as::buffer(topic.data(), topic.size()),
        as::buffer(payload.data(), payload.size()), MQTT_NS::qos::at_most_once,
        props);
    const std::string expected{
        0x30, 17, 0, 1, 't', 12,
        0x02, 0, 0, 1, 0x2C,
        0x26, 0, 1, 'k', 0, 1, 'v',
        'x'};
    const std::string bytes = m.continuous_buffer();
    if (bytes != expected) return fail("v5 PUBLISH bytes");

    const std::string encoded = bytes.substr(6, 12);
    auto parsed = MQTT_NS::v5::property::parse(
        MQTT_NS::buffer(MQTT_NS::string_view(encoded)));
    if (parsed.size() != 2) return fail("v5 property count");
    if (MQTT_NS::variant_get<MQTT_NS::v5::property::message_expiry_interval>(
            parsed[0]).val() != 300) {
      return fail("v5 message_expiry_interval value");
    }
    auto const& up =
        MQTT_NS::variant_get<MQTT_NS::v5::property::user_property>(parsed[1]);
    if (str(up.key()) != "k" || str(up.val()) != "v") {
      return fail("v5 user_property value");
    }
  }

  // The package's MQTT_ALWAYS_SEND_REASON_CODE=1: a successful v5 PUBACK keeps
  // its reason code byte instead of the two-byte form.
  {
    auto m = MQTT_NS::v5::puback_message(
        0x1234, MQTT_NS::v5::puback_reason_code::success, {});
    // continuous_buffer() omits the packet id of this message, so join the
    // wire sequence instead.
    std::string wire;
    for (auto const& b : m.const_buffer_sequence()) {
      wire.append(static_cast<char const*>(b.data()), b.size());
    }
    if (wire != std::string{0x40, 3, 0x12, 0x34, 0}) {
      return fail("v5 PUBACK reason code");
    }
  }

  // The package's MQTT_USE_STR_CHECK: an ill-formed UTF-8 string is rejected.
  {
    using MQTT_NS::utf8string::validate_contents;
    using MQTT_NS::utf8string::validation;
    if (validate_contents("caf\xC3\xA9") != validation::well_formed) {
      return fail("UTF-8 well-formed string");
    }
    if (validate_contents(MQTT_NS::string_view("\xC3\x28", 2)) !=
        validation::ill_formed) {
      return fail("UTF-8 ill-formed string");
    }
    if (validate_contents(MQTT_NS::string_view("a\0b", 3)) !=
        validation::ill_formed) {
      return fail("UTF-8 embedded NUL");
    }
  }

  // The client header set builds against the declared dependencies; nothing
  // connects.
  {
    as::io_context ioc;
    auto c = MQTT_NS::make_sync_client(
        ioc, "localhost", 1883, MQTT_NS::protocol_version::v5);
    c->set_client_id("cid");
    if (c->get_client_id() != "cid" ||
        c->get_protocol_version() != MQTT_NS::protocol_version::v5 ||
        c->connected()) {
      return fail("client construction");
    }
  }

  return 0;
}
