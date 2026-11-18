// Contracts on and thrown, whatever the profile; no stack trace in what().
#define BERTRAND_ENABLE_CONTRACTS
#define BERTRAND_CONTRACTS_ARE_EXCEPTIONS
#define BERTRAND_DISABLE_STACKTRACE
#include <bertrand/bertrand.hpp>

#include <cstdio>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

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

bool starts_with(const std::string &s, const std::string &prefix) {
  return s.compare(0, prefix.size(), prefix) == 0;
}

bool ends_with(const std::string &s, const std::string &suffix) {
  return s.size() >= suffix.size() &&
         s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

} // namespace

int main() {
  int x = -1;

  bool thrown = false;
  int line = 0;
  std::string what;
  std::ostringstream err;
  std::streambuf *cerr_buf = std::cerr.rdbuf(err.rdbuf());
  try {
    // One line, so line is the __LINE__ that Require records.
    line = __LINE__; Require(x > 0, "x must be positive: ", x);
  } catch (const std::runtime_error &e) {
    thrown = true;
    what = e.what();
  }
  std::cerr.rdbuf(cerr_buf);
  CHECK(thrown);
  CHECK(starts_with(what, "Assert at: "));
  CHECK(ends_with(what, ": Require ('x > 0') x must be positive: -1\n"));
  CHECK(what == "Assert at: " + std::string(__FILE__) + ":" +
                    std::to_string(line) +
                    ": Require ('x > 0') x must be positive: -1\n");
  CHECK(err.str() == what);

  thrown = false;
  try {
    Ensure(x < 0, "ok");
    Invariant(true, "ok");
  } catch (const std::runtime_error &) {
    thrown = true;
  }
  CHECK(!thrown);

  std::string ensure_what, invariant_what;
  cerr_buf = std::cerr.rdbuf(err.rdbuf());
  try {
    Ensure(x > 0, "e");
  } catch (const std::runtime_error &e) {
    ensure_what = e.what();
  }
  try {
    Invariant(x == 0, "i");
  } catch (const std::runtime_error &e) {
    invariant_what = e.what();
  }
  std::cerr.rdbuf(cerr_buf);
  CHECK(ends_with(ensure_what, ": Ensure ('x > 0') e\n"));
  CHECK(ends_with(invariant_what, ": Invariant ('x == 0') i\n"));

  CHECK(bertrand::find(2).in(1, 2, 3));
  CHECK(bertrand::find(3).in(1, 2, 3));
  CHECK(!bertrand::find(4).in(1, 2, 3));

  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("bertrand ok");
  return 0;
}
