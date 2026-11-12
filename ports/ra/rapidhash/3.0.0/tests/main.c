/* Consumer: the three hash functions against SMHasher's verification
   values, which pin their output for every input length from 0 to 255,
   each under its own seed, and for a 2048-byte input; here through the
   unrolled loop, in main.cc through the default compact one. */
#define RAPIDHASH_UNROLLED
#include <rapidhash.h>

#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond)                                                  \
  do {                                                               \
    if (!(cond)) {                                                   \
      printf("main.c:%d: check failed: %s\n", __LINE__, #cond);      \
      ++failures;                                                    \
    }                                                                \
  } while (0)

typedef uint64_t (*seeded_fn)(const void *, size_t, uint64_t);
typedef uint64_t (*plain_fn)(const void *, size_t);

/* rurban/smhasher's VerificationTest (KeysetTest.cpp): hash the keys {},
   {0}, {0, 1}, ..., {0, ..., 254} with seed 256 minus the length, then
   hash the 256 results, stored as little-endian bytes, with seed 0; the
   low 32 bits are the verification value. */
static uint32_t verification(seeded_fn hash) {
  unsigned char key[256], hashes[256 * 8];
  int i, k;
  for (i = 0; i < 256; ++i) {
    const uint64_t h = hash(key, (size_t)i, (uint64_t)(256 - i));
    key[i] = (unsigned char)i;
    for (k = 0; k < 8; ++k) hashes[i * 8 + k] = (unsigned char)(h >> (8 * k));
  }
  return (uint32_t)hash(hashes, sizeof hashes, 0);
}

static void check_family(const char *name, seeded_fn with_seed, plain_fn plain,
                         uint32_t expected, size_t same_as_rapidhash) {
  unsigned char data[300], shifted[300 + 8];
  size_t n;
  int before = failures;
  CHECK(verification(with_seed) == expected);
  for (n = 0; n < sizeof data; ++n) data[n] = (unsigned char)(n * 167 + 13);
  for (n = 0; n <= sizeof data; ++n) {
    const uint64_t seed = 0x9E3779B97F4A7C15ull * (n + 1);
    const uint64_t h = with_seed(data, n, seed);
    /* The plain form is seed 0. */
    CHECK(plain(data, n) == with_seed(data, n, 0));
    /* Input alignment does not matter. */
    memcpy(shifted + 3, data, n);
    CHECK(with_seed(shifted + 3, n, seed) == h);
    /* Micro and Nano agree with rapidhash up to 80 and 48 bytes. */
    if (n <= same_as_rapidhash) CHECK(rapidhash_withSeed(data, n, seed) == h);
  }
  if (failures != before) printf("  in %s\n", name);
}

int main(void) {
  check_family("rapidhash", rapidhash_withSeed, rapidhash, 0x1FDC65EEu, 300);
  check_family("rapidhashMicro", rapidhashMicro_withSeed, rapidhashMicro, 0x6F183D61u, 80);
  check_family("rapidhashNano", rapidhashNano_withSeed, rapidhashNano, 0x2C200DC7u, 48);
  printf("rapidhash: %s\n", failures == 0 ? "ok" : "FAILED");
  return failures == 0 ? 0 : 1;
}
