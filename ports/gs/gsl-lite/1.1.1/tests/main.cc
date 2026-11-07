// Consumer test for gsl-lite through the documented header
// <gsl-lite/gsl-lite.hpp> with the default configuration.  The core is C++98,
// the interface floor; finally() and make_unique() need C++11.

#include <gsl-lite/gsl-lite.hpp>

#include <cstddef>
#include <cstdio>
#include <vector>

#if gsl_CPP11_OR_GREATER
#include <memory>
#endif

#define CHECK(cond)                                                       \
  do {                                                                    \
    if (!(cond)) {                                                        \
      std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, \
                   #cond);                                                \
      return 1;                                                           \
    }                                                                     \
  } while (0)

static int sum(gsl_lite::span<const int> values) {
  gsl_Expects(!values.empty());
  int total = 0;
  for (gsl_lite::span<const int>::iterator it = values.begin(); it != values.end(); ++it) total += *it;
  return total;
}

static int deref(gsl_lite::not_null<int*> p) { return *p; }

int main() {
  CHECK(gsl_lite_MAJOR == 1 && gsl_lite_MINOR == 1 && gsl_lite_PATCH == 1);
  // The new header leaves the GSL compatibility mode (namespace gsl, Expects())
  // off and selects the version-1 defaults.
  CHECK(!gsl_FEATURE(GSL_COMPATIBILITY_MODE) && gsl_CONFIG_DEFAULTS_VERSION == 1);
#ifdef Expects
#error "Expects() is defined outside GSL compatibility mode"
#endif

  int values[] = {1, 2, 3, 4, 5};
  gsl_lite::span<int> all(values);
  CHECK(all.size() == 5 && all.data() == values);
  CHECK(sum(all) == 15);
  CHECK(sum(all.subspan(1, 3)) == 9 && sum(all.first(2)) == 3 && sum(all.last(2)) == 9);
  all[0] = 10;
  CHECK(values[0] == 10);

  std::vector<int> vec(values + 1, values + 5);
  CHECK(sum(gsl_lite::make_span(vec)) == 14);
  CHECK(gsl_lite::at(values, 4) == 5 && gsl_lite::at(vec, 0) == 2 && gsl_lite::at(vec, 3) == 5);

  int answer = 42;
  CHECK(deref(gsl_lite::make_not_null(&answer)) == 42);

  // index is the signed std::ptrdiff_t by default.
  gsl_lite::index last = -1;
  CHECK(last < 0 && sizeof(gsl_lite::index) == sizeof(std::ptrdiff_t));

  CHECK(gsl_lite::narrow_cast<unsigned char>(300) == 44);
  CHECK(gsl_lite::narrow_failfast<short>(1000) == 1000);
#if gsl_HAVE(EXCEPTIONS)
  // narrow<>() throws narrowing_error when the value changes, so it needs
  // exceptions.
  CHECK(gsl_lite::narrow<int>(42L) == 42);
  bool threw = false;
  try {
    (void)gsl_lite::narrow<unsigned char>(300);
  } catch (const gsl_lite::narrowing_error&) {
    threw = true;
  }
  CHECK(threw);
  threw = false;
  try {
    (void)gsl_lite::narrow<unsigned>(-1);
  } catch (const gsl_lite::narrowing_error&) {
    threw = true;
  }
  CHECK(threw);
#endif

#if gsl_CPP11_OR_GREATER
  int cleanups = 0;
  {
    auto guard = gsl_lite::finally([&cleanups] { ++cleanups; });
    CHECK(cleanups == 0);
  }
  CHECK(cleanups == 1);

  gsl_lite::not_null<std::unique_ptr<int>> owned = gsl_lite::make_unique<int>(7);
  CHECK(*owned == 7);
#endif

  std::puts("gsl-lite: ok");
  return 0;
}
