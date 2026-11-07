// Encodes a message with protozero's writers and checks the exact wire
// bytes, decodes it back with pbf_reader and pbf_message, and checks the
// exceptions malformed input raises.
#include <protozero/buffer_fixed.hpp>
#include <protozero/buffer_vector.hpp>
#include <protozero/exception.hpp>
#include <protozero/pbf_builder.hpp>
#include <protozero/pbf_message.hpp>
#include <protozero/pbf_reader.hpp>
#include <protozero/pbf_writer.hpp>
#include <protozero/version.hpp>

#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

bool ok = true;

void check(bool pass, const char* what) {
  if (!pass) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ok = false;
  }
}

// The wire format of the message encode() writes, field by field.
const std::string expected =
    std::string("\x08\x96\x01", 3) +                          // 1: uint32 150
    std::string("\x10\x01", 2) +                              // 2: sint32 -1, zigzag 1
    std::string("\x1a\x07" "testing", 9) +                    // 3: string
    std::string("\x25\x78\x56\x34\x12", 5) +                  // 4: fixed32, little-endian
    std::string("\x29\x00\x00\x00\x00\x00\x00\xf8\x3f", 9) +  // 5: double 1.5
    std::string("\x32\x05\x03\x00\x06\xd7\x04", 7) +          // 6: packed sint32 -2 0 3 -300
    std::string("\x3a\x02\x08\x01", 4) +                      // 7: message {1: true}
    std::string("\x40\xff\xff\xff\xff\xff\xff\xff\xff\xff\x01", 11);  // 8: int64 -1

void encode(protozero::pbf_writer& writer) {
  writer.add_uint32(1, 150);
  writer.add_sint32(2, -1);
  writer.add_string(3, "testing");
  writer.add_fixed32(4, 0x12345678);
  writer.add_double(5, 1.5);
  const std::vector<int32_t> packed{-2, 0, 3, -300};
  writer.add_packed_sint32(6, packed.begin(), packed.end());
  {
    protozero::pbf_writer abandoned{writer, 9};
    abandoned.add_string(1, "not written");
    abandoned.rollback();
  }
  {
    protozero::pbf_writer nested{writer, 7};
    nested.add_bool(1, true);
  }
  writer.add_int64(8, -1);
}

void write_and_read() {
  std::string buffer;
  {
    protozero::pbf_writer writer{buffer};
    encode(writer);
  }
  check(buffer == expected, "pbf_writer's wire bytes");

  protozero::pbf_reader reader{buffer};
  int fields = 0;
  while (reader.next()) {
    ++fields;
    switch (reader.tag()) {
      case 1:
        check(reader.get_uint32() == 150, "uint32");
        break;
      case 2:
        check(reader.get_sint32() == -1, "sint32");
        break;
      case 3:
        check(reader.get_view() == protozero::data_view{"testing", 7}, "string view");
        break;
      case 4:
        check(reader.get_fixed32() == 0x12345678, "fixed32");
        break;
      case 5:
        check(reader.get_double() == 1.5, "double");
        break;
      case 6: {
        const auto range = reader.get_packed_sint32();
        const std::vector<int32_t> values(range.begin(), range.end());
        check(range.size() == 4 && values == std::vector<int32_t>({-2, 0, 3, -300}),
              "packed sint32");
        break;
      }
      case 7: {
        protozero::pbf_reader nested = reader.get_message();
        check(nested.next(1, protozero::pbf_wire_type::varint) && nested.get_bool() &&
                  !nested.next(),
              "nested message");
        break;
      }
      case 8:
        check(reader.get_int64() == -1, "int64");
        break;
      default:
        check(false, "an unexpected tag");
        reader.skip();
    }
  }
  check(fields == 8, "eight fields");

  // next(tag) skips the other fields.
  protozero::pbf_reader by_tag{buffer};
  check(by_tag.next(3) && by_tag.get_string() == "testing" && !by_tag.next(3),
        "next(tag) finds the one string");
}

// A submessage of 128 bytes or more needs a two-byte length, so the
// writer must compact the space it reserved for the length.
void long_submessage() {
  const std::string text(200, 'x');
  std::string buffer;
  {
    protozero::pbf_writer writer{buffer};
    protozero::pbf_writer nested{writer, 10};
    nested.add_string(1, text);
  }
  // Field 10 holds 203 bytes (0xcb 0x01): field 1's tag, 200 (0xc8 0x01), the text.
  check(buffer == std::string("\x52\xcb\x01\x0a\xc8\x01", 6) + text, "a two-byte submessage length");

  protozero::pbf_reader reader{buffer};
  check(reader.next(10), "the long submessage");
  protozero::pbf_reader nested = reader.get_message();
  check(nested.next(1) && nested.get_string() == text && !nested.next() && !reader.next(),
        "reading the long submessage back");
}

void other_buffers() {
  std::vector<char> vector_buffer;
  {
    protozero::basic_pbf_writer<std::vector<char>> writer{vector_buffer};
    writer.add_uint32(1, 150);
    writer.add_string(3, "testing");
  }
  check(std::string(vector_buffer.begin(), vector_buffer.end()) == expected.substr(0, 3) + expected.substr(5, 9),
        "a std::vector<char> buffer");

  char storage[4];
  protozero::fixed_size_buffer_adaptor adaptor{storage, sizeof(storage)};
  protozero::basic_pbf_writer<protozero::fixed_size_buffer_adaptor> writer{adaptor};
  writer.add_uint32(1, 150);
  check(adaptor.size() == 3 && std::string(storage, 3) == expected.substr(0, 3),
        "a fixed-size buffer");
  bool exhausted = false;
  try {
    writer.add_string(3, "testing");
  } catch (const std::length_error&) {
    exhausted = true;
  }
  check(exhausted && adaptor.size() <= adaptor.capacity(),
        "a full fixed-size buffer throws std::length_error");

  // Here the tag and the length fit and the string's bytes do not.
  char small[8];
  protozero::fixed_size_buffer_adaptor small_adaptor{small, sizeof(small)};
  protozero::basic_pbf_writer<protozero::fixed_size_buffer_adaptor> small_writer{small_adaptor};
  exhausted = false;
  try {
    small_writer.add_string(3, "testing");
  } catch (const std::length_error&) {
    exhausted = true;
  }
  check(exhausted && small_adaptor.size() == 2, "a string that overflows a fixed-size buffer");
}

enum class Point : protozero::pbf_tag_type { x = 1, y = 2, name = 3 };

void typed_messages() {
  std::string buffer;
  {
    protozero::pbf_builder<Point> builder{buffer};
    builder.add_sint64(Point::x, -300);
    builder.add_sint64(Point::y, 7);
    builder.add_string(Point::name, "origin");
  }
  // zigzag(-300) = 599 = 0xd7 0x04, zigzag(7) = 14.
  check(buffer == std::string("\x08\xd7\x04\x10\x0e\x1a\x06" "origin", 13), "pbf_builder");

  protozero::pbf_message<Point> message{buffer};
  int64_t x = 0;
  int64_t y = 0;
  std::string name;
  while (message.next()) {
    switch (message.tag()) {
      case Point::x:
        x = message.get_sint64();
        break;
      case Point::y:
        y = message.get_sint64();
        break;
      case Point::name:
        name = message.get_string();
        break;
      default:
        message.skip();
    }
  }
  check(x == -300 && y == 7 && name == "origin", "pbf_message");
}

template <typename Exception, typename Read>
bool throws(Read read) {
  try {
    read();
  } catch (const Exception& e) {
    return std::string(e.what()).size() > 0;
  }
  return false;
}

void malformed() {
  const std::string unterminated("\x08\x96", 2);
  check(throws<protozero::end_of_buffer_exception>([&] {
          protozero::pbf_reader reader{unterminated};
          reader.next();
          reader.get_uint32();
        }),
        "an unterminated varint");

  const std::string too_long = std::string("\x08", 1) + std::string(10, '\xff') + '\x01';
  check(throws<protozero::varint_too_long_exception>([&] {
          protozero::pbf_reader reader{too_long};
          reader.next();
          reader.get_uint64();
        }),
        "a varint longer than ten bytes");

  const std::string group("\x0b", 1);  // field 1, wire type 3 (start group)
  check(throws<protozero::unknown_pbf_wire_type_exception>([&] {
          protozero::pbf_reader reader{group};
          reader.next();
        }),
        "a group wire type");

  const std::string tag_zero("\x00\x01", 2);
  check(throws<protozero::invalid_tag_exception>([&] {
          protozero::pbf_reader reader{tag_zero};
          reader.next();
        }),
        "tag 0");

  const std::string short_string("\x1a\x07" "test", 6);
  check(throws<protozero::end_of_buffer_exception>([&] {
          protozero::pbf_reader reader{short_string};
          reader.next();
          reader.get_string();
        }),
        "a string longer than the buffer");
}

}  // namespace

int main() {
  check(std::string(PROTOZERO_VERSION_STRING) == "1.8.2" && PROTOZERO_VERSION_CODE == 10802,
        "version macros");
  write_and_read();
  long_submessage();
  other_buffers();
  typed_messages();
  malformed();
  std::printf("protozero (C++): %s\n", ok ? "ok" : "FAILED");
  return ok ? 0 : 1;
}
