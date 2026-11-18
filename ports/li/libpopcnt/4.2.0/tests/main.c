#include <libpopcnt.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond)                                                            \
  do {                                                                         \
    if (!(cond)) {                                                             \
      fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond); \
      ++failures;                                                              \
    }                                                                          \
  } while (0)

#define BIG 100000

static uint8_t big[BIG];
static uint64_t prefix[BIG + 1];

/* An independent reference: clears one set bit per step. */
static unsigned bits_in(uint8_t v) {
  unsigned n = 0;
  for (; v != 0; v &= (uint8_t)(v - 1))
    ++n;
  return n;
}

int main(void) {
  uint8_t d[256];
  uint32_t state = 12345;
  size_t i, off, len;
  unsigned long mismatches = 0;

  for (i = 0; i < 256; ++i)
    d[i] = (uint8_t)i;
  CHECK(popcnt(d, 256) == 1024);
  CHECK(popcnt(d + 1, 255) == 1024);
  CHECK(popcnt(d, 0) == 0);
  CHECK(popcnt(d + 255, 1) == 8);
  CHECK(popcnt64_bitwise(0xF0F0F0F0F0F0F0F0ULL) == 32);

  memset(big, 0xFF, sizeof big);
  CHECK(popcnt(big, BIG) == 800000);
  big[12345] = 0x0F;
  CHECK(popcnt(big, BIG) == 799996);

  /* Every start offset 0-63 and length 0-1100 crosses the x86 dispatch
     thresholds (40, 96 and 1024 bytes) and NEON's 64-byte chunks at every
     alignment. */
  for (i = 0; i < BIG; ++i) {
    state = state * 1103515245u + 12345u;
    big[i] = (uint8_t)(state >> 16);
    prefix[i + 1] = prefix[i] + bits_in(big[i]);
  }
  for (off = 0; off < 64; ++off) {
    for (len = 0; len <= 1100; ++len) {
      if (popcnt(big + off, len) != prefix[off + len] - prefix[off] &&
          mismatches++ == 0)
        fprintf(stderr, "first mismatch: offset %lu, length %lu\n",
                (unsigned long)off, (unsigned long)len);
    }
  }
  for (i = 0; i + 8 <= BIG; i += 8) {
    uint64_t w;
    memcpy(&w, big + i, sizeof w);
    if (popcnt64_bitwise(w) != prefix[i + 8] - prefix[i])
      ++mismatches;
  }
  CHECK(mismatches == 0);
  for (off = 0; off < 4; ++off) {
    CHECK(popcnt(big + off, 4113) == prefix[off + 4113] - prefix[off]);
    CHECK(popcnt(big + off, BIG - off) == prefix[BIG] - prefix[off]);
  }

  /* Reports the CPUID cache the x86 C build compiles (libpopcnt.h:208-217):
     C11 atomics where available, else the __sync or Interlocked fallback. */
#if defined(LIBPOPCNT_HAVE_C11_ATOMIC)
  puts("cpuid cache: C11 atomics");
#elif defined(LIBPOPCNT_HAVE_CPUID)
  puts("cpuid cache: fallback");
#endif
  if (failures != 0) {
    fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  puts("libpopcnt-c ok");
  return 0;
}
