#include <strong_type.h>

#include <cstdio>
#include <string>
#include <type_traits>

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

STRONG_TYPE(Meters, int);
STRONG_TYPE(Feet, int);
STRONG_TYPE(Name, std::string);

static_assert(!std::is_same_v<Meters, Feet>);
static_assert(!std::is_convertible_v<Feet, Meters>);
static_assert(!std::is_convertible_v<Meters, Feet>);
static_assert(std::is_base_of_v<std::string, Name>);

// Construction from an lvalue is explicit, from an rvalue implicit.
static_assert(!std::is_convertible_v<int &, Meters>);
static_assert(!std::is_convertible_v<const int &, Meters>);
static_assert(std::is_constructible_v<Meters, const int &>);
static_assert(std::is_convertible_v<int, Meters>);
static_assert(!std::is_convertible_v<std::string &, Name>);
static_assert(std::is_convertible_v<std::string, Name>);
// A class-type strong type also takes the class's own constructors.
static_assert(std::is_convertible_v<const char *, Name>);

} // namespace

int main() {
  Meters m(5);
  ++m;
  CHECK(static_cast<int>(m) == 6);
  int old = m++;
  CHECK(old == 6);
  CHECK(static_cast<int>(m) == 7);
  --m;
  CHECK(static_cast<int>(m) == 6);
  old = m--;
  CHECK(old == 6);
  CHECK(static_cast<int>(m) == 5);
  CHECK(m + 1 == 6);
  CHECK(m * Meters(3) == 15);
  CHECK(&++m == &m.value);
  CHECK(&--m == &m.value);
  int &r = m;
  CHECK(&r == &m.value);
  m += 2;
  CHECK(static_cast<int>(m) == 7);

  int five = 5;
  Meters from_lvalue(five);
  CHECK(static_cast<int>(from_lvalue) == 5);
  const Meters cm(4);
  const int &cr = cm;
  CHECK(&cr == &cm.value && cr == 4);
  Meters zero{};
  CHECK(static_cast<int>(zero) == 0);

  Name n = std::string("abc");
  CHECK(n.size() == 3);
  CHECK(n + std::string("d") == "abcd");
  n.append("xy");
  CHECK(n == "abcxy");
  std::string s = "q";
  Name copied(s);
  CHECK(copied == "q" && s == "q");
  Name from_literal = "abc";
  CHECK(from_literal == "abc");
  Name repeated(3, 'x');
  CHECK(repeated == "xxx");
  Name empty;
  CHECK(empty.empty());

  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("enhex-strong_type ok");
  return 0;
}
