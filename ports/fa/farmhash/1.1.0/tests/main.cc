// Expected values are entries of the expected[] tables that farmhash.cc
// prints for its self tests (farmhashmkTest, farmhashxoTest, farmhashnaTest
// and farmhashccTest), for the input of its Setup() and Test(i * i, i).
// Hash32/Hash64/Hash128 are those functions' results on a build without
// SSE4 (the default), tweaked by DebugTweak() unless NDEBUG is defined for
// the library (farmhash.cc:258-267, 389-409).
#include <farmhash.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

namespace {

const uint64_t k0 = 0xc3a5c85c97cb3127ULL;
const uint64_t k1 = 0xb492b66fbe98f273ULL;
const uint32_t c1 = 0xcc9e2d51;

char data[16384];

// Setup() of farmhash.cc:2001-2015 (a prefix of its 1 MiB array).
void Setup() {
  uint64_t a = 9, b = 777;
  for (int i = 0; i < int(sizeof data); i++) {
    a += b;
    b += a;
    a = (a ^ (a >> 41)) * k0;
    b = (b ^ (b >> 41)) * k0 + i;
    data[i] = char(uint8_t(b >> 37));
  }
}

uint32_t Bswap32(uint32_t x) {
  return x << 24 | (x & 0xff00) << 8 | (x >> 8 & 0xff00) | x >> 24;
}
uint64_t Bswap64(uint64_t x) {
  return uint64_t(Bswap32(uint32_t(x))) << 32 | Bswap32(uint32_t(x >> 32));
}
uint32_t Tweak32(uint32_t x) { return ~Bswap32(x * c1); }
uint64_t Tweak64(uint64_t x) { return ~Bswap64(x * k1); }

// Table entries are the high and low 32 bits of each 64-bit result.
uint64_t Join(uint32_t hi, uint32_t lo) { return uint64_t(hi) << 32 | lo; }

struct Row {
  int len;                // Test(len * len, len)
  uint32_t mk;            // farmhashmk::Hash32
  uint32_t nt;            // farmhashnt::Hash32, used when SSE4.1 is on
  uint32_t xo[2];         // farmhashxo::Hash64
  uint32_t na[2];         // farmhashna::Hash64
  uint32_t cc[4];         // farmhashcc::Fingerprint128, low then high
};

// Lengths 0, 5, 20 (<= 32), 40 and 100 (> 32; xo differs from na at 40) of
// upstream's expected[] tables.
const Row rows[] = {
    {0, 3696677242u, 797982799u, {2598464059u, 797982799u}, {2598464059u, 797982799u},
     {1039179260u, 1690343979u, 1018511555u, 2464489001u}},
    {5, 2957164615u, 3696623795u, {2199864459u, 3696623795u}, {2199864459u, 3696623795u},
     {230718965u, 999595115u, 3534822176u, 2175709186u}},
    {20, 3531368319u, 1389770549u, {1099072299u, 1389770549u}, {1099072299u, 1389770549u},
     {2126250818u, 901517871u, 3651631165u, 1323139145u}},
    {40, 3250317967u, 2180594090u, {3006163611u, 2180594090u}, {22638523u, 648000590u},
     {1039773764u, 880219458u, 4275949176u, 1556833823u}},
    {100, 3772933173u, 2005032160u, {95237878u, 2005032160u}, {95237878u, 2005032160u},
     {2101248725u, 3123292429u, 3583524041u, 983372394u}},
};

int failures = 0;

void Check(bool ok, const char* what, int len) {
  if (!ok) {
    fprintf(stderr, "FAIL %s, length %d\n", what, len);
    failures++;
  }
}

}  // namespace

int main() {
  Setup();

  // Hash* results are tweaked unless NDEBUG; Hash128 has one backend, so its
  // empty-input result tells which mode the library was built in.
  const bool tweaked = util::Uint128Low64(util::Hash128(data, 0)) !=
                       Join(rows[0].cc[0], rows[0].cc[1]);

  for (size_t k = 0; k < sizeof rows / sizeof rows[0]; k++) {
    const Row& r = rows[k];
    const char* s = data + r.len * r.len;
    const size_t n = size_t(r.len);

    const uint64_t na = Join(r.na[0], r.na[1]);
    const uint64_t xo = Join(r.xo[0], r.xo[1]);
    const uint64_t lo = Join(r.cc[0], r.cc[1]);
    const uint64_t hi = Join(r.cc[2], r.cc[3]);

    // Fingerprints are fixed on every platform and in every build mode.
    Check(util::Fingerprint32(s, n) == r.mk, "Fingerprint32", r.len);
    Check(util::Fingerprint64(s, n) == na, "Fingerprint64", r.len);
    util::uint128_t f = util::Fingerprint128(s, n);
    Check(util::Uint128Low64(f) == lo && util::Uint128High64(f) == hi,
          "Fingerprint128", r.len);

    // Hash32 is nt where the compiler's default target has SSE4.1 (x86-64
    // macOS), else mk.
    const uint32_t h32 = util::Hash32(s, n);
    Check(h32 == (tweaked ? Tweak32(r.mk) : r.mk) ||
              h32 == (tweaked ? Tweak32(r.nt) : r.nt),
          "Hash32", r.len);
    Check(util::Hash64(s, n) == (tweaked ? Tweak64(xo) : xo), "Hash64",
          r.len);
    uint64_t y = lo, z = hi;
    if (tweaked) {
      y = Tweak64(lo);
      z = Tweak64(hi);
      y += z;
      z += y;
      z *= k1;
    }
    util::uint128_t h = util::Hash128(s, n);
    Check(util::Uint128Low64(h) == y && util::Uint128High64(h) == z,
          "Hash128", r.len);
  }

  return failures != 0;
}
