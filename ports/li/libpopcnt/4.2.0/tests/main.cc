#include <libpopcnt.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

int failures = 0;

#define CHECK(cond)                                                            \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__,    \
                   #cond);                                                     \
      ++failures;                                                              \
    }                                                                          \
  } while (0)

// An independent reference: clears one set bit per step.
unsigned bits_in(std::uint8_t v) {
  unsigned n = 0;
  for (; v != 0; v &= static_cast<std::uint8_t>(v - 1))
    ++n;
  return n;
}

} // namespace

int main() {
  std::uint8_t d[256];
  for (int i = 0; i < 256; ++i)
    d[i] = static_cast<std::uint8_t>(i);
  CHECK(popcnt(d, 256) == 1024);
  CHECK(popcnt(d + 1, 255) == 1024);
  CHECK(popcnt(d, 0) == 0);
  CHECK(popcnt(d + 255, 1) == 8);
  CHECK(popcnt64_bitwise(0xF0F0F0F0F0F0F0F0ULL) == 32);

  const std::size_t big_size = 100000;
  std::vector<std::uint8_t> big(big_size, 0xFF);
  CHECK(popcnt(big.data(), big_size) == 800000);
  big[12345] = 0x0F;
  CHECK(popcnt(big.data(), big_size) == 799996);

  // Every start offset 0-63 and length 0-1100 crosses the x86 dispatch
  // thresholds (40, 96 and 1024 bytes) and NEON's 64-byte chunks at every
  // alignment.
  std::vector<std::uint64_t> prefix(big_size + 1, 0);
  std::uint32_t state = 12345;
  for (std::size_t i = 0; i < big_size; ++i) {
    state = state * 1103515245u + 12345u;
    big[i] = static_cast<std::uint8_t>(state >> 16);
    prefix[i + 1] = prefix[i] + bits_in(big[i]);
  }
  unsigned long mismatches = 0;
  for (std::size_t off = 0; off < 64; ++off) {
    for (std::size_t len = 0; len <= 1100; ++len) {
      if (popcnt(big.data() + off, len) != prefix[off + len] - prefix[off] &&
          mismatches++ == 0)
        std::fprintf(stderr, "first mismatch: offset %zu, length %zu\n", off,
                     len);
    }
  }
  for (std::size_t i = 0; i + 8 <= big_size; i += 8) {
    std::uint64_t w;
    std::memcpy(&w, big.data() + i, sizeof w);
    if (popcnt64_bitwise(w) != prefix[i + 8] - prefix[i])
      ++mismatches;
  }
  CHECK(mismatches == 0);
  for (std::size_t off = 0; off < 4; ++off) {
    CHECK(popcnt(big.data() + off, 4113) == prefix[off + 4113] - prefix[off]);
    CHECK(popcnt(big.data() + off, big_size - off) ==
          prefix[big_size] - prefix[off]);
  }

  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("libpopcnt ok");
  return 0;
}
