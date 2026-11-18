#include <zpp_bits.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <optional>
#include <string>
#include <system_error>
#include <variant>
#include <vector>

#define CHECK(cond)                                                    \
  do {                                                                 \
    if (!(cond)) {                                                     \
      std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #cond); \
      std::exit(1);                                                    \
    }                                                                  \
  } while (0)

static std::vector<std::byte> bytes(std::initializer_list<int> values) {
  std::vector<std::byte> out;
  for (int v : values)
    out.push_back(std::byte(v));
  return out;
}

struct person {
  std::string name;
  int age{};
};

// Without structured-binding packs (GCC at C++20), zpp_bits miscounts an aggregate holding a
// std::optional, so state the members.
struct record {
  using serialize = zpp::bits::members<3>;
  std::vector<std::uint16_t> ids;
  std::optional<std::int32_t> score;
  std::variant<std::int32_t, std::string> tag;
  bool operator==(const record&) const = default;
};

// The default size prefix is four bytes; integers use the native (here little) byte order.
static void test_layout() {
  CHECK(std::endian::native == std::endian::little);

  auto [data, in, out] = zpp::bits::data_in_out();
  CHECK(zpp::bits::success(out(person{"Person1", 25}, person{"Person2", 35})));
  CHECK(data.size() == 30);
  CHECK((std::vector<std::byte>(data.begin(), data.begin() + 15) ==
         bytes({7, 0, 0, 0, 'P', 'e', 'r', 's', 'o', 'n', '1', 25, 0, 0, 0})));

  person p1, p2;
  CHECK(zpp::bits::success(in(p1, p2)));
  CHECK(p1.name == "Person1" && p1.age == 25);
  CHECK(p2.name == "Person2" && p2.age == 35);
}

// A vector, an engaged optional (a true byte, then the value) and a variant (its index byte, then the value).
static void test_containers() {
  const record full{{1, 0x0203}, 7, std::string("ab")};
  const record empty{{}, std::nullopt, -1};

  auto [data, in, out] = zpp::bits::data_in_out();
  CHECK(zpp::bits::success(out(full)));
  CHECK((data == bytes({2, 0, 0, 0, 1, 0, 3, 2, 1, 7, 0, 0, 0, 1, 2, 0, 0, 0, 'a', 'b'})));
  CHECK(zpp::bits::success(out(empty)));
  CHECK(data.size() == 30);
  CHECK((std::vector<std::byte>(data.begin() + 20, data.end()) ==
         bytes({0, 0, 0, 0, 0, 0, 0xFF, 0xFF, 0xFF, 0xFF})));

  record a, b;
  CHECK(zpp::bits::success(in(a, b)));
  CHECK(a == full);
  CHECK(b == empty);
}

static void test_errors() {
  // Two bytes cannot hold an int.
  {
    std::vector<std::byte> input = bytes({1, 2});
    zpp::bits::in in(input);
    int value = 0;
    const std::errc ec = in(value);
    CHECK(ec == std::errc::result_out_of_range);
  }
  // A length of 0x10000000 with no elements after it: alloc_limit refuses it before resizing.
  {
    std::vector<std::byte> input = bytes({0, 0, 0, 0x10});
    zpp::bits::in in(input, zpp::bits::alloc_limit<4096>{});
    std::vector<std::uint8_t> value;
    const std::errc ec = in(value);
    CHECK(ec == std::errc::message_size);
    CHECK(value.empty() && value.capacity() == 0);
  }
  // A variant index out of range.
  {
    std::vector<std::byte> input = bytes({5, 0, 0, 0, 0});
    zpp::bits::in in(input);
    std::variant<std::int32_t, std::string> value;
    const std::errc ec = in(value);
    CHECK(ec == std::errc::bad_message);
  }
}

int main() {
  test_layout();
  test_containers();
  test_errors();
  std::puts("zpp_bits 4.7.7 ok");
  return 0;
}
