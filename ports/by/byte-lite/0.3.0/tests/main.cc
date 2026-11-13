// Consumer test from C++98: conversions to and from integers, the bitwise
// and shift operators and their compound forms keeping eight bits,
// comparisons, and a 16-bit value encoded into and decoded from a byte
// buffer; byte lite's own to_uchar(int) and to_integer without a type
// argument; from C++11 on, to_byte, to_integer, | and << in constant
// expressions.  From C++17 on nonstd::byte is std::byte by default, and the
// same checks, except those for the own byte's extras, cover it.
#include <nonstd/byte.hpp>

#include <cstdio>
#include <cstring>
#if byte_USES_STD_BYTE
#include <type_traits>
#endif

namespace {

int failures = 0;

void check(bool ok, const char* what, int line) {
  if (!ok) {
    std::fprintf(stderr, "main.cc:%d: check failed: %s\n", line, what);
    ++failures;
  }
}
#define CHECK(cond) check((cond), #cond, __LINE__)

// Most significant byte first, whatever the host's byte order.
void encode(unsigned value, nonstd::byte out[2]) {
  out[0] = nonstd::to_byte((value >> 8) & 0xFFu);
  out[1] = nonstd::to_byte(value & 0xFFu);
}

unsigned decode(const nonstd::byte in[2]) {
  return (nonstd::to_integer<unsigned>(in[0]) << 8) |
         nonstd::to_integer<unsigned>(in[1]);
}

}  // namespace

int main() {
  CHECK(std::strcmp(byte_lite_VERSION, "0.3.0") == 0);
  CHECK(sizeof(nonstd::byte) == 1);
#if byte_USES_STD_BYTE
  CHECK((std::is_same<nonstd::byte, std::byte>::value));
#endif

  nonstd::byte b = nonstd::to_byte(0x5A);
  CHECK(nonstd::to_integer<int>(b) == 0x5A);
  CHECK(nonstd::to_integer<unsigned>(b) == 0x5Au);
  CHECK(nonstd::to_uchar(b) == 0x5A);
  CHECK(nonstd::to_integer<int>(nonstd::to_byte(0xFF)) == 255);

  const nonstd::byte high = nonstd::to_byte(0xF0);
  const nonstd::byte mixed = nonstd::to_byte(0x3C);
  CHECK((high | mixed) == nonstd::to_byte(0xFC));
  CHECK((high & mixed) == nonstd::to_byte(0x30));
  CHECK((high ^ mixed) == nonstd::to_byte(0xCC));
  CHECK(~high == nonstd::to_byte(0x0F));

  // Shifts drop the bits shifted out of the byte.
  CHECK((nonstd::to_byte(0x81) << 1) == nonstd::to_byte(0x02));
  CHECK((nonstd::to_byte(0x81) >> 1) == nonstd::to_byte(0x40));
  nonstd::byte shifted = nonstd::to_byte(0xAB);
  CHECK((shifted <<= 4) == nonstd::to_byte(0xB0));
  CHECK((shifted >>= 4) == nonstd::to_byte(0x0B));
  CHECK(shifted == nonstd::to_byte(0x0B));

  nonstd::byte flags = nonstd::to_byte(0x01);
  flags |= nonstd::to_byte(0x84);
  CHECK(flags == nonstd::to_byte(0x85));
  flags &= nonstd::to_byte(0x0F);
  CHECK(flags == nonstd::to_byte(0x05));
  flags ^= nonstd::to_byte(0x05);
  CHECK(flags == nonstd::to_byte(0x00));

  CHECK(high != mixed && !(high == mixed));
  CHECK(mixed < high && mixed <= high && high <= nonstd::to_byte(0xF0));
  CHECK(high > mixed && high >= mixed && !(mixed > high));

#if !byte_USES_STD_BYTE
  CHECK(nonstd::to_uchar(0x1A5) == 0xA5);
  CHECK(nonstd::to_integer(b) == 0x5A);
#endif

  nonstd::byte buffer[2];
  encode(0xBEEFu, buffer);
  CHECK(buffer[0] == nonstd::to_byte(0xBE) && buffer[1] == nonstd::to_byte(0xEF));
  CHECK(decode(buffer) == 0xBEEFu);

#if byte_CPP11_OR_GREATER
  static_assert(nonstd::to_integer<int>(nonstd::to_byte(0x12)) == 0x12, "");
  static_assert((nonstd::to_byte(0x12) | nonstd::to_byte(0x01)) ==
                    nonstd::to_byte(0x13),
                "");
  static_assert((nonstd::to_byte(0x81) << 1) == nonstd::to_byte(0x02), "");
#endif

  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("byte-lite consumer test passed");
  return 0;
}
