// Consumer test from C++98: construction from bool, contextual conversion
// in if, ?: and &&, conversion back to bool, negation, and all six
// comparisons on every pair of values, each returning a boolean_ that
// converts to the expected bool.  From C++11 on, where boolean_ converts
// implicitly only from and to bool, also those conversion rules, the
// explicit conversions from and to int, and constexpr and noexcept use.
#include <nonstd/boolean.hpp>

#include <cstdio>
#include <cstring>
#if !boolean_CPP98_FALLBACK
#include <type_traits>
#endif

namespace {

int failures = 0;

void check(bool ok, const char* what, int line) {
  if (!ok) {
    std::fprintf(stderr, "main.cc:%d: check failed: %s\n", line, what);
    ++failures;
  }
}
#define CHECK(cond) check((cond), #cond, __LINE__)

// Takes a boolean_ and branches on it.
int pick(nonstd::boolean_ first, int a, int b) { return first ? a : b; }

}  // namespace

int main() {
  CHECK(std::strcmp(boolean_lite_VERSION, "0.1.0") == 0);

  const nonstd::boolean_ yes(true);
  const nonstd::boolean_ no(false);

  int taken = 0;
  if (yes) taken += 1;
  if (no) taken += 10;
  if (!no) taken += 100;
  CHECK(taken == 101);
  CHECK(yes && !no);
  CHECK(pick(yes, 1, 2) == 1 && pick(no, 1, 2) == 2);

  const bool from_yes = yes;
  CHECK(from_yes && !static_cast<bool>(no));
  CHECK(static_cast<bool>(!!yes));

  const bool values[] = {false, true};
  for (int i = 0; i < 2; ++i) {
    for (int j = 0; j < 2; ++j) {
      const bool x = values[i];
      const bool y = values[j];
      const nonstd::boolean_ a(x);
      const nonstd::boolean_ b(y);
      CHECK(static_cast<bool>(a == b) == (x == y));
      CHECK(static_cast<bool>(a != b) == (x != y));
      CHECK(static_cast<bool>(a < b) == (x < y));
      CHECK(static_cast<bool>(a > b) == (x > y));
      CHECK(static_cast<bool>(a <= b) == (x <= y));
      CHECK(static_cast<bool>(a >= b) == (x >= y));
    }
  }

#if !boolean_CPP98_FALLBACK
  static_assert(std::is_convertible<bool, nonstd::boolean_>::value, "");
  static_assert(!std::is_convertible<int, nonstd::boolean_>::value, "");
  static_assert(std::is_convertible<nonstd::boolean_, bool>::value, "");
  static_assert(!std::is_convertible<nonstd::boolean_, int>::value, "");
  static_assert(std::is_constructible<nonstd::boolean_, int>::value, "");
  static_assert(std::is_constructible<int, nonstd::boolean_>::value, "");
  static_assert(std::is_same<decltype(!yes), nonstd::boolean_>::value, "");
  static_assert(std::is_same<decltype(yes == no), nonstd::boolean_>::value, "");
  static_assert(std::is_same<decltype(yes != no), nonstd::boolean_>::value, "");
  static_assert(std::is_same<decltype(yes < no), nonstd::boolean_>::value, "");
  static_assert(std::is_same<decltype(yes > no), nonstd::boolean_>::value, "");
  static_assert(std::is_same<decltype(yes <= no), nonstd::boolean_>::value, "");
  static_assert(std::is_same<decltype(yes >= no), nonstd::boolean_>::value, "");

  const nonstd::boolean_ implicit = true;
  CHECK(implicit == yes);
  CHECK(static_cast<bool>(nonstd::boolean_(42)) && !static_cast<bool>(nonstd::boolean_(0)));
  CHECK(static_cast<int>(yes) == 1 && static_cast<int>(no) == 0);

  static_assert(nonstd::boolean_(true) != nonstd::boolean_(false), "");
  static_assert(!nonstd::boolean_(false), "");
  static_assert(noexcept(nonstd::boolean_(true) < nonstd::boolean_(false)), "");
#endif

  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("boolean-lite consumer test passed");
  return 0;
}
