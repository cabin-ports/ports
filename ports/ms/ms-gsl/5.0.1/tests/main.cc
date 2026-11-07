// Consumer test: span views and subspans, checked narrowing, not_null,
// final_action, byte, dyn_array, and finally a bounds violation, which
// GSL's contracts must turn into std::terminate.
#include <gsl/gsl>

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <numeric>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char* what, int line) {
  if (!ok) {
    std::fprintf(stderr, "main.cc:%d: check failed: %s\n", line, what);
    ++failures;
  }
}
#define CHECK(cond) check((cond), #cond, __LINE__)

int sum(gsl::span<const int> values) {
  return std::accumulate(values.begin(), values.end(), 0);
}

int deref(gsl::not_null<const int*> p) { return *p; }

}  // namespace

int main() {
  int raw[] = {1, 2, 3, 4, 5};
  gsl::span<int> all(raw);
  CHECK(all.size() == 5);
  CHECK(sum(all) == 15);
  CHECK(sum(all.subspan(1, 3)) == 9);
  CHECK(sum(all.first(2)) == 3);
  CHECK(sum(all.last<2>()) == 9);
  all[0] = 10;
  CHECK(raw[0] == 10);

  std::vector<int> vec = {7, 8, 9};
  CHECK(sum(vec) == 24);
  CHECK(gsl::at(vec, 2) == 9);

  // narrow() throws when the value does not survive the conversion.
  CHECK(gsl::narrow<unsigned char>(200) == 200);
  bool threw = false;
  try {
    (void)gsl::narrow<unsigned char>(300);
  } catch (const gsl::narrowing_error&) {
    threw = true;
  }
  CHECK(threw);
  threw = false;
  try {
    (void)gsl::narrow<unsigned int>(-1);
  } catch (const gsl::narrowing_error&) {
    threw = true;
  }
  CHECK(threw);
  CHECK(gsl::narrow_cast<unsigned char>(300) == 44);

  const int forty_two = 42;
  CHECK(deref(&forty_two) == 42);

  int cleanups = 0;
  {
    auto cleanup = gsl::finally([&] { ++cleanups; });
    CHECK(cleanups == 0);
  }
  CHECK(cleanups == 1);

  gsl::byte b = gsl::to_byte<0x5A>();
  b <<= 1;
  CHECK(gsl::to_integer<int>(b) == 0xB4);

  gsl::dyn_array<int> arr(4, 3);
  CHECK(arr.size() == 4);
  CHECK(std::accumulate(arr.begin(), arr.end(), 0) == 12);

  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }

  // An out-of-range index violates gsl::at's precondition, which must
  // terminate.  Reaching the line after it means contracts are off.
  std::set_terminate([] {
    std::puts("ms-gsl consumer test passed");
    std::fflush(stdout);
    std::_Exit(0);
  });
  int index = 5;
  int value = gsl::at(raw, index);
  std::fprintf(stderr, "gsl::at(raw, 5) returned %d instead of terminating\n",
               value);
  return 1;
}
