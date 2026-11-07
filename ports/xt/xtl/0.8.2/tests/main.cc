// RFC 4648 base64 vectors, the SMHasher verification values of the
// MurmurHash2 variants upstream's tests expect, IEEE half-precision rounding, and xoptional and
// xbasic_fixed_string through xjson.hpp's nlohmann::json overloads.
#include <xtl/xbase64.hpp>
#include <xtl/xbasic_fixed_string.hpp>
#include <xtl/xdynamic_bitset.hpp>
#include <xtl/xhalf_float.hpp>
#include <xtl/xhash.hpp>
#include <xtl/xjson.hpp>
#include <xtl/xoptional.hpp>
#include <xtl/xplatform.hpp>
#include <xtl/xsystem.hpp>
#include <xtl/xtl_config.hpp>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

static_assert(XTL_VERSION_MAJOR == 0 && XTL_VERSION_MINOR == 8 &&
                  XTL_VERSION_PATCH == 2,
              "xtl 0.8.2");

static int failures = 0;

static void check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

static bool little_endian_host() {
  const std::uint32_t one = 1;
  unsigned char first;
  std::memcpy(&first, &one, 1);
  return first == 1;
}

static void base64() {
  const char* const plain[] = {"", "f", "fo", "foo", "foob", "fooba", "foobar"};
  const char* const coded[] = {"",         "Zg==",     "Zm8=",    "Zm9v",
                               "Zm9vYg==", "Zm9vYmE=", "Zm9vYmFy"};
  for (int i = 0; i < 7; ++i) {
    check(xtl::base64encode(plain[i]) == coded[i], "base64encode");
    check(xtl::base64decode(coded[i]) == plain[i], "base64decode");
  }
}

// SMHasher's VerificationTest: hash {}, {0}, {0, 1}, ... with seed
// 256 - length, then hash the concatenated results (in memory order) with
// seed 0 and read its first four bytes as a little-endian integer.
template <class H, class F>
static std::uint32_t verification(F hash) {
  unsigned char key[256];
  unsigned char hashes[256 * sizeof(H)];
  for (unsigned i = 0; i < 256; ++i) {
    key[i] = static_cast<unsigned char>(i);
    const H h = hash(key, i, static_cast<H>(256 - i));
    std::memcpy(hashes + i * sizeof(H), &h, sizeof(H));
  }
  const H h = hash(hashes, sizeof hashes, H(0));
  unsigned char r[sizeof(H)];
  std::memcpy(r, &h, sizeof(H));
  return std::uint32_t(r[0]) | std::uint32_t(r[1]) << 8 |
         std::uint32_t(r[2]) << 16 | std::uint32_t(r[3]) << 24;
}

static void hashes() {
  // The values upstream's test/test_xhash.cpp:150-166 expects of
  // hash_bytes: MurmurHash64A on 64-bit hosts, by byte order, and the
  // 32-bit MurmurHash2 where std::size_t is 32 bits.
#if INTPTR_MAX == INT64_MAX
  const std::uint32_t expected =
      little_endian_host()
          ? (sizeof(std::size_t) == 4 ? 0x27864C1Eu : 0x1F0D3804u)
          : 0x8FDA498Du;
#elif INTPTR_MAX == INT32_MAX
  const std::uint32_t expected =
      sizeof(std::size_t) == 4 ? 0x27864C1Eu : 0xDD537C05u;
#endif
  check(verification<std::size_t>(&xtl::hash_bytes) == expected,
        "hash_bytes verification");
  // SMHasher's MurmurHash2 value, published for little-endian hosts.
  if (little_endian_host())
    check(verification<std::uint32_t>(&xtl::murmur2_x86) == 0x27864C1Eu,
          "MurmurHash2 verification");
}

static void platform() {
  check((xtl::endianness() == xtl::endian::little_endian) ==
            little_endian_host(),
        "endianness");
  const std::string exe = xtl::executable_path();
  const std::string prefix = xtl::prefix_path();
  check(!exe.empty() && prefix.size() < exe.size() &&
            exe.compare(0, prefix.size(), prefix) == 0,
        "prefix_path is a prefix of executable_path");
}

static void optional_values() {
  xtl::xoptional<double> unset;
  check(!unset.has_value(), "default xoptional is missing");
  xtl::xoptional<double> two = 2.0;
  check(two.has_value() && two.value() == 2.0, "xoptional value");
  check(!(two + xtl::missing<double>()).has_value(),
        "missing propagates through +");
  const xtl::xoptional<double> five = two + xtl::xoptional<double>(3.0);
  check(five.has_value() && five.value() == 5.0, "xoptional +");
  check(xtl::missing<double>().value_or(-1.0) == -1.0, "value_or");

  // A flag held by reference: assigning a value sets the external flag.
  double storage = 0.0;
  bool flag = false;
  auto view = xtl::optional(storage, flag);
  check(!view.has_value(), "closure starts missing");
  view = 4.5;
  check(flag && storage == 4.5, "closure writes through");

  const nlohmann::json null_json = xtl::missing<double>();
  check(null_json.is_null(), "missing to JSON null");
  check(!null_json.get<xtl::xoptional<double>>().has_value(),
        "JSON null to missing");
  const nlohmann::json number = two;
  check(number.dump() == "2.0", "xoptional to JSON number");
  check(nlohmann::json::parse("[1.5]")[0].get<xtl::xoptional<double>>() == 1.5,
        "JSON number to xoptional");
}

static void fixed_strings() {
  using str16 = xtl::xbasic_fixed_string<char, 16, xtl::buffer | xtl::store_size,
                                         xtl::string_policy::throwing_error>;
  // The size is stored in the buffer's last byte: no extra storage.
  static_assert(sizeof(xtl::xbasic_fixed_string<char, 255, xtl::buffer | xtl::store_size,
                                                xtl::string_policy::throwing_error>) == 256,
                "minimal storage");
  str16 s = "hello";
  s += " world";
  check(s.size() == 11 && s == "hello world", "append");
  check(s.find("wor") == 6 && s.substr(6, 3) == "wor", "find and substr");
  bool threw = false;
  try {
    s += " and more";
  } catch (const std::length_error&) {
    threw = true;
  }
  check(threw, "throwing_error rejects 20 characters");

  const nlohmann::json j = s;
  check(j.get<std::string>() == "hello world", "fixed string to JSON");
  check(nlohmann::json("abc").get<str16>() == "abc", "JSON to fixed string");
}

static void half_floats() {
  using half = xtl::half_float;
  static_assert(sizeof(half) == 2, "binary16");
  check(float(std::numeric_limits<half>::max()) == 65504.0f, "max");
  check(float(half(1.0f / 3.0f)) == 0.333251953125f, "1/3 rounds to 0x3555");
  // Round to nearest, ties to even: 2049 lies halfway between 2048 and
  // 2050, and 65520 halfway between 65504 and the next step, infinity.
  check(float(half(2049.0f)) == 2048.0f, "ties to even");
  check(std::isinf(float(half(65520.0f))), "overflow to infinity");
  check(float(half(2.0f) * half(3.0f) + half(0.5f)) == 6.5f, "arithmetic");
}

static void bitsets() {
  xtl::xdynamic_bitset<std::uint8_t> bits(70, false);
  check(bits.size() == 70 && bits.block_count() == 9 && bits.none(),
        "70 bits in 9 byte blocks");
  bits.set(3);
  bits.set(69);
  check(bits.count() == 2 && bits[69] && !bits[68], "set");
  check(bits.data()[0] == 0x08 && bits.data()[8] == 0x20, "block layout");
  bits.flip();
  check(bits.count() == 68 && !bits[3] && !bits.all(), "flip");
  xtl::xdynamic_bitset<std::uint8_t> mask(70, true);
  mask &= bits;
  check(mask == bits, "and");
}

int main() {
  base64();
  hashes();
  platform();
  optional_values();
  fixed_strings();
  half_floats();
  bitsets();
  if (failures != 0) return 1;
  std::puts("xtl consumer ok");
  return 0;
}
