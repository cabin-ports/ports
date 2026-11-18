// Consumer test for cabin-ports/libmorton: 2D and 3D, 32- and 64-bit Morton
// encode and decode through morton.h's default (portable) functions, and the
// for-loop, lookup-table, magic-bits and combined magic-bits methods directly.
#include <libmorton/morton.h>

#include <cstdio>

static int failures = 0;

static void check(bool pass, const char *what) {
  if (!pass) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

// x takes the even bits and y the odd bits: 5 = 0b101, 9 = 0b1001 -> 0b10010011.
static const uint_fast32_t code_5_9 = 147;
// x, y and z take bits 3k, 3k+1 and 3k+2: (1, 2, 3) -> 0b110101.
static const uint_fast32_t code_1_2_3 = 53;

static void test_default_functions() {
  uint_fast16_t x = 0, y = 0, z = 0;
  check(libmorton::morton2D_32_encode(5, 9) == code_5_9, "2D 32-bit encode");
  libmorton::morton2D_32_decode(code_5_9, x, y);
  check(x == 5 && y == 9, "2D 32-bit decode");

  uint_fast32_t X = 0, Y = 0, Z = 0;
  check(libmorton::morton2D_64_encode(0xFFFFFFFFu, 0) == 0x5555555555555555ull, "2D 64-bit encode fills the even bits");
  libmorton::morton2D_64_decode(0xAAAAAAAAAAAAAAAAull, X, Y);
  check(X == 0 && Y == 0xFFFFFFFFu, "2D 64-bit decode of the odd bits");

  check(libmorton::morton3D_32_encode(1, 2, 3) == code_1_2_3, "3D 32-bit encode");
  libmorton::morton3D_32_decode(code_1_2_3, x, y, z);
  check(x == 1 && y == 2 && z == 3, "3D 32-bit decode");

  // 1023 sets x bits 0..9, at code bits 0, 3, ..., 27: (8^10 - 1) / 7.
  check(libmorton::morton3D_64_encode(1023, 0, 0) == 153391689u, "3D 64-bit encode");
  libmorton::morton3D_64_decode(uint_fast64_t(153391689u) << 2, X, Y, Z);
  check(X == 0 && Y == 0 && Z == 1023, "3D 64-bit decode of z");
}

static void test_methods_agree() {
  check(libmorton::m2D_e_for<uint_fast64_t, uint_fast32_t>(5, 9) == code_5_9, "2D for-loop encode");
  // The early-termination variants go through morton_common.h's bit scan.
  check(libmorton::m2D_e_for_ET<uint_fast64_t, uint_fast32_t>(5, 9) == code_5_9, "2D early-termination encode");
  check(libmorton::m3D_e_for_ET<uint_fast64_t, uint_fast32_t>(1, 2, 3) == code_1_2_3, "3D early-termination encode");
  check(libmorton::m2D_e_magicbits_combined(5, 9) == code_5_9, "2D combined magic-bits encode");
  uint_fast16_t x = 0, y = 0;
  libmorton::m2D_d_magicbits_combined(code_5_9, x, y);
  check(x == 5 && y == 9, "2D combined magic-bits decode");

  check(libmorton::m3D_e_sLUT<uint_fast64_t, uint_fast32_t>(1, 2, 3) == code_1_2_3, "3D shifted-LUT encode");
  check(libmorton::m3D_e_magicbits<uint_fast64_t, uint_fast32_t>(1, 2, 3) == code_1_2_3, "3D magic-bits encode");
  uint_fast32_t X = 0, Y = 0, Z = 0;
  libmorton::m3D_d_LUT<uint_fast64_t, uint_fast32_t>(code_1_2_3, X, Y, Z);
  check(X == 1 && Y == 2 && Z == 3, "3D LUT decode");

  // All 21 bits of each coordinate: x fills bits 0, 3, ..., 60.
  check(libmorton::morton3D_64_encode(0x1FFFFF, 0, 0) == 0x1249249249249249ull, "3D 64-bit encode of 21-bit x");
  libmorton::morton3D_64_decode(0x7FFFFFFFFFFFFFFFull, X, Y, Z);
  check(X == 0x1FFFFF && Y == 0x1FFFFF && Z == 0x1FFFFF, "3D 64-bit decode of 63 set bits");

  // Round trips over a spread of coordinates.
  for (uint_fast32_t i = 0; i < 2000; i += 37) {
    uint_fast32_t a = i, b = i * 7, c = i * 13;
    libmorton::morton3D_64_decode(libmorton::morton3D_64_encode(a, b, c), X, Y, Z);
    check(X == a && Y == b && Z == c, "3D 64-bit round trip");
  }
}

int main() {
  test_default_functions();
  test_methods_agree();
  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("libmorton 0.2.13 C++ ok");
  return 0;
}
