#include <itoa/jeaiii_to_text.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

static int failures = 0;

template <class T>
static void check(T v, const char *expected) {
  char buf[64];
  std::memset(buf, 'x', sizeof buf);
  char *end = jeaiii::to_text_from_integer(buf, v);
  std::string got(buf, end);
  if (got != expected) {
    std::printf("FAIL: expected %s, got %s\n", expected, got.c_str());
    ++failures;
  }
}

int main() {
  // 32-bit unsigned, at each digit-count boundary.
  check<std::uint32_t>(0, "0");
  check<std::uint32_t>(9, "9");
  check<std::uint32_t>(10, "10");
  check<std::uint32_t>(99, "99");
  check<std::uint32_t>(100, "100");
  check<std::uint32_t>(999, "999");
  check<std::uint32_t>(1000, "1000");
  check<std::uint32_t>(9999, "9999");
  check<std::uint32_t>(10000, "10000");
  check<std::uint32_t>(99999, "99999");
  check<std::uint32_t>(100000, "100000");
  check<std::uint32_t>(999999, "999999");
  check<std::uint32_t>(1000000, "1000000");
  check<std::uint32_t>(9999999, "9999999");
  check<std::uint32_t>(10000000, "10000000");
  check<std::uint32_t>(99999999, "99999999");
  check<std::uint32_t>(100000000, "100000000");
  check<std::uint32_t>(999999999, "999999999");
  check<std::uint32_t>(1000000000, "1000000000");
  check<std::uint32_t>(4294967295u, "4294967295");
  check<std::uint32_t>(1234567890u, "1234567890");

  // 32-bit signed.
  check<std::int32_t>(-1, "-1");
  check<std::int32_t>(-10, "-10");
  check<std::int32_t>(-99999, "-99999");
  check<std::int32_t>(-100000, "-100000");
  check<std::int32_t>(2147483647, "2147483647");
  check<std::int32_t>(-2147483647 - 1, "-2147483648");

  // Narrow types.
  check<std::uint8_t>(255, "255");
  check<std::int8_t>(-128, "-128");
  check<std::uint16_t>(65535, "65535");
  check<std::int16_t>(-32768, "-32768");

  // 64-bit unsigned: every digit count from 10 to 20, each side of the boundary.
  check<std::uint64_t>(4294967296ull, "4294967296");
  check<std::uint64_t>(9999999999ull, "9999999999");
  check<std::uint64_t>(10000000000ull, "10000000000");
  check<std::uint64_t>(99999999999ull, "99999999999");
  check<std::uint64_t>(100000000000ull, "100000000000");
  check<std::uint64_t>(999999999999ull, "999999999999");
  check<std::uint64_t>(1000000000000ull, "1000000000000");
  check<std::uint64_t>(9999999999999ull, "9999999999999");
  check<std::uint64_t>(10000000000000ull, "10000000000000");
  check<std::uint64_t>(99999999999999ull, "99999999999999");
  check<std::uint64_t>(100000000000000ull, "100000000000000");
  check<std::uint64_t>(999999999999999ull, "999999999999999");
  check<std::uint64_t>(1000000000000000ull, "1000000000000000");
  check<std::uint64_t>(9999999999999999ull, "9999999999999999");
  check<std::uint64_t>(10000000000000000ull, "10000000000000000");
  check<std::uint64_t>(99999999999999999ull, "99999999999999999");
  check<std::uint64_t>(100000000000000000ull, "100000000000000000");
  check<std::uint64_t>(999999999999999999ull, "999999999999999999");
  check<std::uint64_t>(1000000000000000000ull, "1000000000000000000");
  check<std::uint64_t>(9999999999999999999ull, "9999999999999999999");
  check<std::uint64_t>(10000000000000000000ull, "10000000000000000000");
  check<std::uint64_t>(12345678901234567890ull, "12345678901234567890");
  check<std::uint64_t>(18446744073709551615ull, "18446744073709551615");
  check<std::uint64_t>(18446744073709551614ull, "18446744073709551614");
  check<std::uint64_t>(11111111111111111111ull, "11111111111111111111");
  check<std::uint64_t>(99999999899999999ull, "99999999899999999");
  check<std::uint64_t>(10000000099999999ull, "10000000099999999");
  check<std::uint64_t>(123456789012345678ull, "123456789012345678");

  // Low or middle 8-digit groups that are small (leading zeros inside the number).
  check<std::uint64_t>(10000000100000001ull, "10000000100000001");
  check<std::uint64_t>(100000000000000001ull, "100000000000000001");
  check<std::uint64_t>(10000000000000000001ull, "10000000000000000001");
  check<std::uint64_t>(10000000000000009ull, "10000000000000009");
  check<std::uint64_t>(1000000010000000ull, "1000000010000000");
  check<std::uint64_t>(18000000000000000123ull, "18000000000000000123");
  check<std::int64_t>(-100000000000000001ll, "-100000000000000001");
  check<std::int64_t>(1000000000000000007ll, "1000000000000000007");

  // 64-bit signed.
  check<std::int64_t>(9223372036854775807ll, "9223372036854775807");
  check<std::int64_t>(-9223372036854775807ll - 1, "-9223372036854775808");
  check<std::int64_t>(-4294967296ll, "-4294967296");
  check<std::int64_t>(-1234567890123456789ll, "-1234567890123456789");
  check<std::int64_t>(-99999999999999999ll, "-99999999999999999");

  if (failures != 0)
    return 1;
  std::puts("ok");
  return 0;
}
