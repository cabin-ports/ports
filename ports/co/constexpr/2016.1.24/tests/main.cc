#include <cx_fnv1.h>
#include <cx_math.h>
#include <cx_md5.h>
#include <cx_murmur3.h>
#include <cx_sha256.h>
#include <cx_utils.h>

#include <cmath>
#include <cstdio>

// Runtime calls of cx:: functions fail to link by design, so each value is
// computed in a constant expression, asserted there, and read again at run time.
constexpr unsigned long long fnv1 = cx::fnv1("hello, world");
constexpr unsigned long long fnv1a = cx::fnv1a("hello, world");
static_assert(fnv1 == 733686394982303293ull, "fnv1");
static_assert(fnv1a == 1702823495152329533ull, "fnv1a");

constexpr unsigned murmur = cx::murmur3_32("hello, world", 0);
constexpr unsigned murmur_seeded = cx::murmur3_32("hello, world", 1);
static_assert(murmur == 345750399u, "murmur3_32");
static_assert(murmur_seeded == 1868346089u, "murmur3_32 seeded");

constexpr cx::md5sum md5abc = cx::md5("abc");
static_assert(cx::endianswap(md5abc.h[0]) == 0x90015098u &&
              cx::endianswap(md5abc.h[1]) == 0x3cd24fb0u &&
              cx::endianswap(md5abc.h[2]) == 0xd6963f7du &&
              cx::endianswap(md5abc.h[3]) == 0x28e17f72u, "md5");

constexpr cx::sha256sum sha256abc = cx::sha256("abc");
static_assert(cx::endianswap(sha256abc.h[0]) == 0xba7816bfu &&
              cx::endianswap(sha256abc.h[1]) == 0x8f01cfeau &&
              cx::endianswap(sha256abc.h[2]) == 0x414140deu &&
              cx::endianswap(sha256abc.h[3]) == 0x5dae2223u &&
              cx::endianswap(sha256abc.h[4]) == 0xb00361a3u &&
              cx::endianswap(sha256abc.h[5]) == 0x96177a9cu &&
              cx::endianswap(sha256abc.h[6]) == 0xb410ff61u &&
              cx::endianswap(sha256abc.h[7]) == 0xf20015adu, "sha256");

constexpr int len = cx::strlen("hello, world");
constexpr int less = cx::strcmp("abc", "abd");
constexpr int greater = cx::strcmp("abc", "abb");
static_assert(len == 12 && less == -1 && greater == 1, "strlen, strcmp");

constexpr double sqrt2 = cx::sqrt(2.0);
constexpr double cbrt2 = cx::cbrt(2.0);
constexpr double exp1 = cx::exp(1.0);
constexpr double log2_8 = cx::log2(8.0);
constexpr double sin1 = cx::sin(1.0);
constexpr double cos1 = cx::cos(1.0);
constexpr double atan1 = cx::atan(1.0);
constexpr double pow_2_10 = cx::pow(2.0, 10.0);
constexpr double floor_m = cx::floor(-2.5);
constexpr double hypot34 = cx::hypot(3.0, 4.0);
static_assert(cx::abs(sqrt2 - 1.41421356237309505) < 1e-12, "sqrt");
static_assert(floor_m == -3.0 && hypot34 > 4.999999999 && hypot34 < 5.000000001,
              "floor, hypot");

static int failures = 0;

static void check(bool ok, const char* what) {
  if (!ok) {
    std::printf("FAIL: %s\n", what);
    ++failures;
  }
}

static void close(double got, double want, const char* what) {
  check(std::fabs(got - want) <= 1e-9 * (1.0 + std::fabs(want)), what);
}

int main() {
  check(fnv1 == 733686394982303293ull && fnv1a == 1702823495152329533ull, "fnv1/fnv1a");
  check(murmur == 345750399u && murmur_seeded == 1868346089u, "murmur3_32");
  check(cx::endianswap(md5abc.h[0]) == 0x90015098u &&
            cx::endianswap(md5abc.h[3]) == 0x28e17f72u, "md5");
  check(cx::endianswap(sha256abc.h[0]) == 0xba7816bfu &&
            cx::endianswap(sha256abc.h[7]) == 0xf20015adu, "sha256");
  check(len == 12 && less == -1 && greater == 1, "strlen/strcmp");

  close(sqrt2, std::sqrt(2.0), "sqrt");
  close(cbrt2, std::cbrt(2.0), "cbrt");
  close(exp1, std::exp(1.0), "exp");
  close(log2_8, std::log2(8.0), "log2");
  close(sin1, std::sin(1.0), "sin");
  close(cos1, std::cos(1.0), "cos");
  close(atan1, std::atan(1.0), "atan");
  close(pow_2_10, std::pow(2.0, 10.0), "pow");
  close(floor_m, std::floor(-2.5), "floor");
  close(hypot34, std::hypot(3.0, 4.0), "hypot");

  if (failures == 0) std::puts("ok");
  return failures != 0;
}
