// snowhouse: passing assertions return normally; a failing AssertThat throws
// snowhouse::AssertionException with upstream's exact message and the
// assertion's file and line, and AssertThrows reports a missing or wrong
// exception.
#include <snowhouse/snowhouse.h>

#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

using namespace snowhouse;

namespace {

int failures = 0;

void check(bool ok, int line, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "main.cc:%d: check failed: %s\n", line, what);
    ++failures;
  }
}

#define CHECK(expr) check(static_cast<bool>(expr), __LINE__, #expr)

// The message of the AssertionException that fn throws, or "no exception".
template <typename F>
std::string failure_of(F fn) {
  try {
    fn();
  } catch (const AssertionException& e) {
    return e.what();
  }
  return "no exception";
}

struct Point {
  int x;
  int y;
};

bool operator==(const Point& a, const Point& b) {
  return a.x == b.x && a.y == b.y;
}

void maybe_throw(bool do_throw) {
  if (do_throw) {
    throw std::out_of_range("index 10");
  }
}

}  // namespace

// Upstream's documented hook for printing a type in failure messages.
namespace snowhouse {
template <>
struct Stringizer<Point> {
  static std::string ToString(const Point& p) {
    return "(" + std::to_string(p.x) + ", " + std::to_string(p.y) + ")";
  }
};
}  // namespace snowhouse

int main() {
  CHECK(std::strcmp(SNOWHOUSE_VERSION, "5.0.0") == 0);

  const std::vector<int> v{1, 2, 3, 5, 8};
  const Point p12{1, 2};
  const Point p13{1, 3};

  CHECK(failure_of([&] {
          AssertThat(5, Equals(5));
          AssertThat(5, Is().EqualTo(5));
          AssertThat(4, Is().Not().EqualTo(5));
          AssertThat(7, Is().GreaterThan(5).And().LessThan(10));
          AssertThat(v, Contains(5));
          AssertThat(v, HasLength(5));
          AssertThat(v, Has().All().GreaterThan(0));
          AssertThat(v, Has().Exactly(1).EqualTo(8));
          AssertThat(std::string("hello world"),
                     Is().StartingWith("hello").And().EndingWith("world"));
          AssertThat(3.1, EqualsWithDelta(3.0, 0.2));
          AssertThat(p12, Equals(p12));
          AssertThat(true, IsTrue());
        }) == "no exception");

  // The exception records where the assertion was written.
  {
    bool thrown = false;
    const unsigned line = __LINE__ + 2;
    try {
      AssertThat(5, Equals(6));
    } catch (const AssertionException& e) {
      thrown = true;
      CHECK(std::string(e.what()) == "Expected: equal to 6\nActual: 5\n");
      CHECK(e.file() == __FILE__ && e.line() == line);
    }
    CHECK(thrown);
  }

  CHECK(failure_of([] { AssertThat(5, Is().GreaterThan(5)); }) ==
        "Expected: greater than 5\nActual: 5\n");
  CHECK(failure_of([] {
          AssertThat(7, Is().GreaterThan(5).And().LessThan(6));
        }) == "Expected: greater than 5 and less than 6\nActual: 7\n");
  CHECK(failure_of([&] { AssertThat(v, Has().None().EqualTo(5)); }) ==
        "Expected: none equal to 5\nActual: [ 1, 2, 3, 5, 8 ]\n");
  CHECK(failure_of([] { AssertThat("wrong", Is().EqualTo("right")); }) ==
        "Expected: equal to \"right\"\nActual: \"wrong\"\n");
  CHECK(failure_of([&] { AssertThat(p12, Equals(p13)); }) ==
        "Expected: equal to (1, 3)\nActual: (1, 2)\n");

  // AssertThrows stores the caught exception, and fails on no exception or
  // on one of another type.
  CHECK(failure_of([] {
          AssertThrows(std::out_of_range, maybe_throw(true));
          AssertThat(LastException<std::out_of_range>().what(),
                     Equals("index 10"));
        }) == "no exception");
  CHECK(failure_of([] { AssertThrows(std::logic_error, maybe_throw(false)); }) ==
        "Expected std::logic_error. No exception was thrown.");
  CHECK(failure_of([] {
          AssertThrows(std::invalid_argument, maybe_throw(true));
        }) ==
        "Expected std::invalid_argument. Wrong exception was thrown. "
        "Description of unwanted exception: index 10");

  if (failures != 0) {
    return 1;
  }
  std::printf("snowhouse ok\n");
  return 0;
}
