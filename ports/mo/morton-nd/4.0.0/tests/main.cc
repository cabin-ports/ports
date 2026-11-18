// mortonND_BMI2.h is empty unless the target has BMI2 or AVX2.
#include <morton-nd/mortonND_BMI2.h>
#include <morton-nd/mortonND_LUT.h>

#include <cstdint>
#include <cstdio>
#include <tuple>

namespace {

int failures = 0;

void check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

constexpr auto kEncoder2D = mortonnd::MortonNDLutEncoder_2D_32();
const mortonnd::MortonNDLutDecoder_2D_32 kDecoder2D{};
// 3 fields of 10 bits with 8-bit lookup tables.
constexpr auto kEncoder3D = mortonnd::MortonNDLutEncoder<3, 10, 8>();
const mortonnd::MortonNDLutDecoder<3, 10, 8> kDecoder3D{};

// The tables are built at compile time; the first field takes the low bit.
static_assert(kEncoder2D.Encode(5, 9) == 147, "2D encode at compile time");
static_assert(kEncoder3D.Encode(1, 2, 3) == 53, "3D encode at compile time");

void TestKnownCodes() {
  check(kEncoder2D.Encode(5, 9) == 147, "2D encode");
  std::uint_fast32_t x = 0, y = 0;
  std::tie(x, y) = kDecoder2D.Decode(147);
  check(x == 5 && y == 9, "2D decode");
  check(kEncoder2D.Encode(0xFFFF, 0) == 0x55555555u &&
            kEncoder2D.Encode(0, 0xFFFF) == 0xAAAAAAAAu,
        "2D encode of full fields");

  check(mortonnd::MortonNDLutEncoder<2, 32, 8>().Encode(0xFFFFFFFFu, 0) ==
            0x5555555555555555u,
        "2D encode of 32-bit fields");

  std::uint_fast32_t a = 0, b = 0, c = 0;
  std::tie(a, b, c) = kDecoder3D.Decode(53);
  check(kEncoder3D.Encode(1, 2, 3) == 53 && a == 1 && b == 2 && c == 3,
        "3D encode and decode");
  check(kEncoder3D.Encode(1023, 0, 0) == 153391689, "3D encode of a full field");
}

void TestRoundTrips() {
  bool ok = true;
  for (std::uint_fast32_t x = 0; x < 65536; x += 997) {
    for (std::uint_fast32_t y = 0; y < 65536; y += 1009) {
      std::uint_fast32_t dx = 0, dy = 0;
      std::tie(dx, dy) = kDecoder2D.Decode(kEncoder2D.Encode(x, y));
      ok = ok && dx == x && dy == y;
    }
  }
  check(ok, "2D round trips");

  ok = true;
  for (std::uint_fast32_t x = 0; x < 1024; x += 101) {
    for (std::uint_fast32_t y = 0; y < 1024; y += 103) {
      for (std::uint_fast32_t z = 0; z < 1024; z += 107) {
        std::uint_fast32_t dx = 0, dy = 0, dz = 0;
        std::tie(dx, dy, dz) = kDecoder3D.Decode(kEncoder3D.Encode(x, y, z));
        ok = ok && dx == x && dy == y && dz == z;
      }
    }
  }
  check(ok, "3D round trips");
}

}  // namespace

int main() {
  TestKnownCodes();
  TestRoundTrips();
  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("morton-nd consumer test passed");
  return 0;
}
