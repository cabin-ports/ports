// Consumer test from C++98: empty and engaged states, access and its
// failure, value_or, reset, assignment, comparison, swap, make_optional,
// and the lifetime of a non-default-constructible value; from C++11 on,
// emplace and a move-only value.
#include <nonstd/optional.hpp>

#include <cstdio>
#include <cstring>
#include <utility>
#if optional_CPP11_OR_GREATER
#include <memory>
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

// Counts live instances; has no default constructor.
struct Tracked {
  static int live;
  int value;
  explicit Tracked(int v) : value(v) { ++live; }
  Tracked(const Tracked& other) : value(other.value) { ++live; }
  Tracked& operator=(const Tracked& other) {
    value = other.value;
    return *this;
  }
  ~Tracked() { --live; }
};
int Tracked::live = 0;

nonstd::optional<int> parse_digit(char c) {
  if (c < '0' || c > '9') return nonstd::nullopt;
  return c - '0';
}

}  // namespace

int main() {
  CHECK(std::strcmp(optional_lite_VERSION, "3.6.0") == 0);

  // Empty and engaged states.
  nonstd::optional<int> empty;
  nonstd::optional<int> five(5);
  CHECK(!empty.has_value() && !empty && empty == nonstd::nullopt);
  CHECK(five.has_value() && *five == 5 && five.value() == 5);
  CHECK(empty.value_or(7) == 7 && five.value_or(7) == 5);
  CHECK(parse_digit('8').value_or(-1) == 8 && !parse_digit('x'));

  // value() on an empty optional throws bad_optional_access.
  bool thrown = false;
  try {
    (void)empty.value();
  } catch (const nonstd::bad_optional_access&) {
    thrown = true;
  }
  CHECK(thrown);

  // Reset and assignment.
  five.reset();
  CHECK(!five);
  five = 6;
  CHECK(five && *five == 6);
  five = nonstd::nullopt;
  CHECK(!five);

  // Comparison: an empty optional orders before every value.
  nonstd::optional<int> one(1), two(2);
  CHECK(one < two && !(two < one) && one != two && one == 1 && 2 == two);
  CHECK(empty < one && !(one < empty) && empty <= empty && two > 1);

  // Swapping an engaged and an empty optional moves the value across.
  swap(one, empty);
  CHECK(!one && empty && *empty == 1);

  CHECK(*nonstd::make_optional(3) == 3);

  // The contained value is constructed and destroyed exactly once.
  {
    nonstd::optional<Tracked> tracked(Tracked(4));
    CHECK(Tracked::live == 1 && tracked->value == 4);
    tracked.reset();
    CHECK(Tracked::live == 0 && !tracked);
    tracked = Tracked(5);
    CHECK(Tracked::live == 1 && tracked->value == 5);
    nonstd::optional<Tracked> copy(tracked);
    CHECK(Tracked::live == 2 && copy->value == 5);
  }
  CHECK(Tracked::live == 0);

#if optional_CPP11_OR_GREATER
  nonstd::optional<std::pair<int, int> > pair;
  pair.emplace(3, 4);
  CHECK(pair && pair->first == 3 && pair->second == 4);
  nonstd::optional<std::unique_ptr<int> > owner(std::unique_ptr<int>(new int(9)));
  nonstd::optional<std::unique_ptr<int> > moved(std::move(owner));
  CHECK(moved && **moved == 9);
#endif

  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("optional-lite consumer test passed");
  return 0;
}
