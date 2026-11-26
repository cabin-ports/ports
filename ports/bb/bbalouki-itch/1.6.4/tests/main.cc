#include <itch/encoder.hpp>
#include <itch/parser.hpp>
#include <itch/price.hpp>

#include <cstddef>
#include <iostream>
#include <variant>
#include <vector>

static int failures = 0;

#define CHECK(cond)                                                      \
  do {                                                                   \
    if (!(cond)) {                                                       \
      std::cerr << __FILE__ << ":" << __LINE__ << ": " << #cond << "\n"; \
      ++failures;                                                        \
    }                                                                    \
  } while (0)

int main() {
  // Length 12, System Event: locate 0, tracking 1, timestamp 12345, code 'O'.
  const unsigned char frame[] = {0x00, 0x0C, 'S',  0x00, 0x00, 0x00, 0x01,
                                 0x00, 0x00, 0x00, 0x00, 0x30, 0x39, 'O'};
  const auto* data = reinterpret_cast<const char*>(frame);

  itch::Parser parser;
  const std::vector<itch::Message> messages = parser.parse(data, sizeof frame);
  CHECK(messages.size() == 1);
  if (messages.size() == 1) {
    const auto* event = std::get_if<itch::SystemEventMessage>(&messages[0]);
    CHECK(event != nullptr);
    if (event != nullptr) {
      CHECK(event->message_type == 'S');
      CHECK(event->stock_locate == 0);
      CHECK(event->tracking_number == 1);
      CHECK(event->timestamp == 12345);
      CHECK(event->event_code == 'O');
    }

    const std::vector<std::byte> encoded = itch::encode_frame(messages[0]);
    CHECK(encoded.size() == sizeof frame);
    for (std::size_t i = 0; i < encoded.size() && i < sizeof frame; ++i) {
      CHECK(static_cast<unsigned char>(encoded[i]) == frame[i]);
    }
  }

  const itch::StandardPrice price = itch::make_price(1234500);
  CHECK(price.raw() == 1234500);
  CHECK(price.to_double() == 123.45);
  CHECK(price.to_string() == "123.4500");

  return failures == 0 ? 0 : 1;
}
