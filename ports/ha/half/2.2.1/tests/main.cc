// Consumer test for cabin-ports/half in its default configuration: conversions
// with IEEE binary16 bit patterns, correctly rounded arithmetic and sqrt,
// std::numeric_limits, half_cast with explicit rounding modes, special values,
// and the manually managed exception flags.
#include <half.hpp>

#include <cstdio>
#include <cstring>
#include <limits>

using half_float::half;
using half_float::half_cast;

static int failures = 0;

static void check(bool pass, const char *what) {
  if (!pass) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

static unsigned bits(half h) {
  unsigned short u = 0;
  std::memcpy(&u, &h, sizeof(u));
  return u;
}

static void test_conversions() {
  check(sizeof(half) == 2, "half is two bytes");
  check(bits(half(1.0f)) == 0x3C00 && bits(half(-2.0f)) == 0xC000, "1 and -2 encode as 0x3C00 and 0xC000");
  check(bits(half(65504.0f)) == 0x7BFF, "65504 is the largest finite half");
  // 0.1 lies between 0x2E66 (0.0999755859375) and 0x2E67; the first is nearer.
  check(bits(half(0.1f)) == 0x2E66 && static_cast<float>(half(0.1f)) == 0.0999755859375f, "0.1 rounds to nearest");
  check(bits(half(5.9604644775390625e-8f)) == 0x0001, "2^-24 is the smallest subnormal");
  check(isinf(half(1.0e5f)), "1e5 overflows to infinity");
#if HALF_ENABLE_CPP11_USER_LITERALS
  using namespace half_float::literal;
  check(bits(0.1_h) == 0x2E66, "the _h literal rounds like the constructor");
#endif
}

static void test_arithmetic() {
  half a(1.5f), b(2.25f);
  check(static_cast<float>(a + b) == 3.75f && static_cast<float>(a * b) == 3.375f, "exact sum and product");
  // 1/3 lies between 0x3555 (0.333251953125) and 0x3556 (0.33349609375).
  check(bits(half(1.0f) / half(3.0f)) == 0x3555, "1/3 rounds to nearest");
  // sqrt(2) = 1.41421356..., between 1.4140625 (0x3DA8) and 1.4150390625.
  check(bits(sqrt(half(2.0f))) == 0x3DA8, "sqrt(2) is correctly rounded");
  check(bits(fma(half(2.0f), half(3.0f), half(-1.0f))) == bits(half(5.0f)), "fma");
  check(lround(half(2.5f)) == 3 && lround(half(-2.5f)) == -3, "lround rounds halfway cases away from zero");
  check(bits(nextafter(half(1.0f), half(2.0f))) == 0x3C01, "nextafter steps one ULP");
}

static void test_limits() {
  typedef std::numeric_limits<half> lim;
  check(lim::is_specialized && lim::digits == 11 && lim::max_exponent == 16, "numeric_limits shape");
  check(static_cast<float>(lim::epsilon()) == 0.0009765625f, "epsilon is 2^-10");
  check(static_cast<float>(lim::max()) == 65504.0f, "max is 65504");
  check(static_cast<float>(lim::min()) == 6.103515625e-05f, "min normal is 2^-14");
  check(isinf(lim::infinity()) && isnan(lim::quiet_NaN()) && !isnan(lim::max()), "infinity and NaN");
}

static void test_half_cast() {
  // The README's examples (float in place of its C++11 literals), then 1/3.
  check(half_cast<int, std::round_to_nearest>(half(0.7f)) == 1, "half_cast to int rounds to nearest");
  check(static_cast<float>(half_cast<half, std::round_toward_zero>(4097)) == 4096.0f, "4097 toward zero");
  check(static_cast<float>(half_cast<half, std::round_toward_infinity>(4097)) == 4100.0f, "4097 toward infinity");
  half tiny = half_cast<half, std::round_toward_infinity>(std::numeric_limits<double>::min());
  check(tiny > half(0.0f) && bits(tiny) == 0x0001, "the smallest positive normal double rounds up to the smallest subnormal");
  check(bits(half_cast<half, std::round_toward_infinity>(1.0f / 3.0f)) == 0x3556, "1/3 toward infinity");
}

static void test_special_values_and_flags() {
  half_float::feclearexcept(FE_ALL_EXCEPT);
  half big = std::numeric_limits<half>::max() * half(2.0f);
  half nan = sqrt(half(-1.0f));
  check(isinf(big) && big > std::numeric_limits<half>::max(), "overflow gives infinity");
  check(isnan(nan) && !(nan == nan), "sqrt(-1) is a NaN that compares unequal to itself");
  // Without HALF_ERRHANDLING_FLAGS only the manual functions touch the flags.
  check(half_float::fetestexcept(FE_ALL_EXCEPT) == 0, "arithmetic leaves the flags alone");

  half_float::feraiseexcept(FE_OVERFLOW | FE_INEXACT);
  check(half_float::fetestexcept(FE_ALL_EXCEPT) == (FE_OVERFLOW | FE_INEXACT), "feraiseexcept sets the flags");
  half_float::feclearexcept(FE_OVERFLOW);
  check(half_float::fetestexcept(FE_ALL_EXCEPT) == FE_INEXACT, "feclearexcept clears only the named flag");
}

int main() {
  test_conversions();
  test_arithmetic();
  test_limits();
  test_half_cast();
  test_special_values_and_flags();
  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("half 2.2.1 C++ ok");
  return 0;
}
