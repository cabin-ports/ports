// Exercises cnl::scaled_integer construction, to_rep, arithmetic and the
// widened result type of multiplication, with exact values.

#include <cnl/all.h>

#include <cstdint>
#include <cstdio>
#include <type_traits>

using cnl::power;
using cnl::scaled_integer;

static int failures = 0;

static void check(bool ok, const char *what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

int main() {
  auto x = scaled_integer<int, power<-1>>{3.5};
  check(to_rep(x) == 7, "to_rep of 3.5 at power<-1>");
  check(static_cast<double>(x) == 3.5, "round-trip 3.5");
  x /= 2;
  check(static_cast<double>(x) == 1.5, "3.5 / 2 == 1.5");
  check(to_rep(x) == 3, "to_rep after division");

  auto y = scaled_integer<std::uint8_t, power<-4>>{15.9375};
  check(to_rep(y) == 255, "to_rep of 15.9375 at power<-4>");
  auto yy = y * y;
  static_assert(
      std::is_same<decltype(yy), scaled_integer<int, power<-8>>>::value,
      "multiplication widens to int, power<-8>");
  check(static_cast<double>(yy) == 254.00390625, "15.9375 squared");
  check(static_cast<double>(scaled_integer<std::uint8_t, power<-4>>{yy}) == 14.0,
        "narrowing back wraps modulo 2^8 steps");

  auto e = cnl::make_elastic_scaled_integer(y) * cnl::make_elastic_scaled_integer(y);
  static_assert(
      std::is_same<decltype(e), cnl::elastic_scaled_integer<16, -8, unsigned>>::value,
      "elastic multiplication keeps all digits");
  check(static_cast<double>(e) == 254.00390625, "elastic 15.9375 squared");

  auto sum = scaled_integer<int, power<-2>>{1.25} + scaled_integer<int, power<-2>>{2.5};
  check(static_cast<double>(sum) == 3.75, "addition");

  if (failures == 0) std::puts("ok");
  return failures == 0 ? 0 : 1;
}
