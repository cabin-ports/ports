// Consumer test from C++98: empty and engaged states, type(), any_cast by
// value and through a pointer and its failure, copies that own their value,
// assignment of another type, swap and reset, and the lifetime of the held
// value; from C++11 on, moves, make_any, in_place_type and emplace.  From
// C++17 on nonstd::any is std::any, and the same checks cover it.
#include <nonstd/any.hpp>

#include <cstdio>
#include <cstring>
#include <string>
#include <typeinfo>
#if any_CPP11_OR_GREATER
#include <utility>
#include <vector>
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

}  // namespace

int main() {
  CHECK(std::strcmp(any_lite_VERSION, "0.4.0") == 0);

  nonstd::any empty;
  CHECK(!empty.has_value() && empty.type() == typeid(void));
  CHECK(nonstd::any_cast<int>(&empty) == NULL);

  // Access by value and, in place, through a pointer: any lite's own
  // any_cast<T&> does not compile (see cabin.toml).
  nonstd::any number = 42;
  CHECK(number.has_value() && number.type() == typeid(int));
  CHECK(nonstd::any_cast<int>(number) == 42);
  *nonstd::any_cast<int>(&number) = 43;
  CHECK(nonstd::any_cast<int>(number) == 43);
  const nonstd::any& constant = number;
  CHECK(*nonstd::any_cast<int>(&constant) == 43);
  CHECK(nonstd::any_cast<double>(&number) == NULL);

  // A cast to the wrong type throws bad_any_cast, a std::bad_cast.
  bool thrown = false;
  try {
    (void)nonstd::any_cast<double>(number);
  } catch (const nonstd::bad_any_cast& e) {
    thrown = e.what() != NULL && e.what()[0] != '\0';
  }
  CHECK(thrown);
  thrown = false;
  try {
    (void)nonstd::any_cast<std::string>(constant);
  } catch (const std::bad_cast&) {
    thrown = true;
  }
  CHECK(thrown);

  // A copy owns its own value.
  nonstd::any text = std::string("abc");
  nonstd::any copy = text;
  *nonstd::any_cast<std::string>(&copy) += "def";
  CHECK(nonstd::any_cast<std::string>(text) == "abc");
  CHECK(nonstd::any_cast<std::string>(copy) == "abcdef");

  // Assigning a value of another type replaces the held type.
  number = std::string("x");
  CHECK(number.type() == typeid(std::string));
  CHECK(nonstd::any_cast<std::string>(number) == "x");

  nonstd::swap(number, empty);
  CHECK(!number.has_value() && nonstd::any_cast<std::string>(empty) == "x");
  number.swap(empty);
  CHECK(number.has_value() && !empty.has_value());
  number.reset();
  CHECK(!number.has_value() && number.type() == typeid(void));

  // The held value is copied with the any and destroyed with it, on reset
  // and on assignment.
  {
    nonstd::any held = Tracked(4);
    CHECK(Tracked::live == 1 && nonstd::any_cast<Tracked>(&held)->value == 4);
    nonstd::any second = held;
    CHECK(Tracked::live == 2);
    held.reset();
    CHECK(Tracked::live == 1);
    second = 7;
    CHECK(Tracked::live == 0 && nonstd::any_cast<int>(second) == 7);
    held = Tracked(5);
  }
  CHECK(Tracked::live == 0);

#if any_CPP11_OR_GREATER
  nonstd::any moved = std::move(text);
  CHECK(nonstd::any_cast<std::string>(moved) == "abc");
  CHECK(nonstd::any_cast<int>(nonstd::any(7)) == 7);
  nonstd::any made = nonstd::make_any<std::string>(3, 'x');
  CHECK(nonstd::any_cast<std::string>(made) == "xxx");
  nonstd::any list(nonstd::in_place_type<std::vector<int> >, {1, 2, 3});
  CHECK(nonstd::any_cast<std::vector<int> >(&list)->size() == 3);
  number.emplace<std::string>(2, 'y');
  CHECK(nonstd::any_cast<std::string>(number) == "yy");
#endif

  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("any-lite consumer test passed");
  return 0;
}
