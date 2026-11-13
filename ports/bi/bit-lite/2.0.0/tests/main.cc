// Consumer test from C++98: the C++20 <bit> functions on unsigned char,
// unsigned short and unsigned int against hand-computed values (powers of
// two, rotations by negative and full-width counts, the five counting
// functions), bit_cast between float and its IEEE-754 bits, byteswap,
// endian against the host's byte order, and the endian conversion
// extensions, checked through the bytes in memory where the result depends
// on the host; from C++11 on, counting, powers of two, rotation, byteswap
// and endian conversion on unsigned long long, and from C++14 on, some of
// the functions in constant expressions.  From C++20 on nonstd's functions
// are std's by default, and the same checks cover them.
#include <nonstd/bit.hpp>

#include <cstdio>
#include <cstring>

namespace {

int failures = 0;

void check(bool ok, const char* what, int line) {
  if (!ok) {
    std::fprintf(stderr, "main.cc:%d: check failed: %s\n", line, what);
    ++failures;
  }
}
#define CHECK(cond) check((cond), #cond, __LINE__)

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

// Whether v's bytes in memory, first to last, are b0, b1, b2 and b3.
bool bytes_are(u32 v, int b0, int b1, int b2, int b3) {
  unsigned char b[sizeof v];
  std::memcpy(b, &v, sizeof v);
  return sizeof v == 4 && b[0] == b0 && b[1] == b1 && b[2] == b2 &&
         b[3] == b3;
}

}  // namespace

int main() {
  CHECK(std::strcmp(bit_lite_VERSION, "2.0.0") == 0);

  CHECK(!nonstd::has_single_bit(0u) && nonstd::has_single_bit(1u));
  CHECK(nonstd::has_single_bit(64u) && !nonstd::has_single_bit(65u));
  CHECK(nonstd::has_single_bit(u8(0x80)) && !nonstd::has_single_bit(u8(0x81)));
  CHECK(nonstd::bit_ceil(0u) == 1u && nonstd::bit_ceil(1u) == 1u);
  CHECK(nonstd::bit_ceil(5u) == 8u && nonstd::bit_ceil(64u) == 64u);
  CHECK(nonstd::bit_ceil(65u) == 128u);
  CHECK(nonstd::bit_ceil(u8(5)) == 8 && nonstd::bit_ceil(u8(128)) == 128);
  CHECK(nonstd::bit_ceil(u16(1000)) == 1024);
  CHECK(nonstd::bit_floor(0u) == 0u && nonstd::bit_floor(1u) == 1u);
  CHECK(nonstd::bit_floor(5u) == 4u && nonstd::bit_floor(0xFFFFFFFFu) == 0x80000000u);
  CHECK(nonstd::bit_floor(u8(0xFF)) == 0x80);
  CHECK(nonstd::bit_width(0u) == 0 && nonstd::bit_width(1u) == 1);
  CHECK(nonstd::bit_width(5u) == 3 && nonstd::bit_width(0xFFFFFFFFu) == 32);
  CHECK(nonstd::bit_width(u8(0x80)) == 8);

  // Rotations wrap, also for types narrower than int; a negative count
  // rotates the other way, and the count is taken modulo the width.
  CHECK(nonstd::rotl(u8(0x81), 1) == 0x03 && nonstd::rotr(u8(0x81), 1) == 0xC0);
  const u32 word = 0x12345678u;
  CHECK(nonstd::rotl(word, 8) == 0x34567812u && nonstd::rotr(word, 8) == 0x78123456u);
  CHECK(nonstd::rotl(word, -8) == 0x78123456u && nonstd::rotr(word, -8) == 0x34567812u);
  CHECK(nonstd::rotl(word, 0) == word && nonstd::rotl(word, 32) == word);
  CHECK(nonstd::rotr(word, 36) == 0x81234567u);

  CHECK(nonstd::countl_zero(u8(0x10)) == 3 && nonstd::countl_zero(1u) == 31);
  CHECK(nonstd::countl_zero(0u) == 32);
  CHECK(nonstd::countl_one(u8(0xF0)) == 4 && nonstd::countl_one(0xFFFFFFFFu) == 32);
  CHECK(nonstd::countl_one(0x7FFFFFFFu) == 0);
  CHECK(nonstd::countr_zero(0x10u) == 4 && nonstd::countr_zero(u16(0)) == 16);
  CHECK(nonstd::countr_one(0x0Fu) == 4 && nonstd::countr_one(u8(0xFF)) == 8);
  CHECK(nonstd::popcount(0xF0F0u) == 8 && nonstd::popcount(u8(0)) == 0);
  CHECK(nonstd::popcount(0xFFFFFFFFu) == 32 && nonstd::popcount(u16(0x8001)) == 2);

  CHECK(nonstd::bit_cast<u32>(1.0f) == 0x3F800000u);
  CHECK(nonstd::bit_cast<u32>(-2.0f) == 0xC0000000u);
  CHECK(nonstd::bit_cast<float>(0x3F000000u) == 0.5f);

#if bit_HAVE_BYTESWAP || bit_CPP23_OR_GREATER
  CHECK(nonstd::byteswap(u8(0xAB)) == 0xAB);
  CHECK(nonstd::byteswap(u16(0x1234)) == 0x3412);
  CHECK(nonstd::byteswap(0x12345678u) == 0x78563412u);
#endif

  const u32 one = 1;
  unsigned char first_byte = 0;
  std::memcpy(&first_byte, &one, 1);
  CHECK((nonstd::endian::native == nonstd::endian::little) == (first_byte == 1));
  CHECK((nonstd::endian::native == nonstd::endian::big) == (first_byte == 0));

  // to_big_endian(v) and to_little_endian(v) swap unconditionally,
  // to_native_endian(v) and as_native_endian(v) return v, to_*_endian(v, e)
  // converts a v of byte order e, and as_big_endian(v) and
  // as_little_endian(v) convert a native v.
  const u32 value = 0x01020304u;
  CHECK(nonstd::to_big_endian(value) == 0x04030201u);
  CHECK(nonstd::to_little_endian(value) == 0x04030201u);
  CHECK(nonstd::to_native_endian(value) == value);
  CHECK(nonstd::to_big_endian(u16(0x0102)) == 0x0201);
  CHECK(nonstd::to_big_endian(value, nonstd::big_endian_type()) == value);
  CHECK(nonstd::to_big_endian(value, nonstd::little_endian_type()) == 0x04030201u);
  CHECK(nonstd::to_little_endian(value, nonstd::little_endian_type()) == value);
  CHECK(bytes_are(nonstd::as_big_endian(value), 1, 2, 3, 4));
  CHECK(bytes_are(nonstd::as_little_endian(value), 4, 3, 2, 1));
  CHECK(nonstd::as_native_endian(value) == value);
  CHECK(nonstd::to_native_endian(nonstd::as_big_endian(value),
                                 nonstd::big_endian_type()) == value);
  CHECK(nonstd::to_native_endian(nonstd::as_little_endian(value),
                                 nonstd::little_endian_type()) == value);

#if bit_CPP11_OR_GREATER
  typedef unsigned long long u64;
  const u64 top = u64(1) << 63;
  CHECK(nonstd::popcount(~u64(0)) == 64 && nonstd::countl_zero(u64(1)) == 63);
  CHECK(nonstd::countr_zero(u64(1) << 40) == 40 && nonstd::bit_width(u64(1) << 40) == 41);
  CHECK(nonstd::bit_floor(~u64(0)) == top && nonstd::bit_ceil(top - 1) == top);
  CHECK(nonstd::rotl(u64(1), 63) == top && nonstd::rotr(u64(1), 1) == top);
#if bit_HAVE_BYTESWAP || bit_CPP23_OR_GREATER
  CHECK(nonstd::byteswap(u64(0x0102030405060708ull)) == 0x0807060504030201ull);
#endif
  CHECK(nonstd::to_big_endian(u64(0x0102030405060708ull)) == 0x0807060504030201ull);
#endif

#if bit_CPP14_OR_GREATER
  static_assert(nonstd::has_single_bit(64u) && nonstd::bit_ceil(5u) == 8u, "");
  static_assert(nonstd::popcount(0xF0u) == 4 && nonstd::countl_zero(1u) == 31, "");
  static_assert(nonstd::rotl(0x80000001u, 1) == 3u, "");
#endif

  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("bit-lite consumer test passed");
  return 0;
}
