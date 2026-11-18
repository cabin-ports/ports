// Consumer test for cabin-ports/strong_type: strong typedefs built from the
// README's modifiers, checking both what each modifier enables (with exact
// values) and, at compile time, the conversions a strong type must reject.
// Written in C++14, upstream's minimum.
#include <strong_type/strong_type.hpp>

#include <cstdio>
#include <functional>
#include <iterator>
#include <limits>
#include <sstream>
#include <string>
#include <type_traits>
#include <unordered_set>
#include <vector>

using meters = strong::type<int, struct meters_, strong::arithmetic, strong::ordered, strong::equality,
                            strong::iostreamable, strong::hashable, strong::bicrementable>;
using other = strong::type<int, struct other_, strong::equality>;
using flags = strong::type<unsigned, struct flags_, strong::bitarithmetic, strong::equality, strong::boolean>;
using ratio = strong::type<int, struct ratio_, strong::convertible_to<double>, strong::equality_with<int>,
                           strong::ordered_with<int>, strong::regular>;
using timestamp = strong::type<long, struct timestamp_, strong::affine_point<>, strong::equality>;
using names = strong::type<std::vector<std::string>, struct names_, strong::range, strong::indexed<>>;

// A strong type is a distinct type that converts neither way implicitly.
static_assert(!std::is_convertible<meters, int>::value, "no implicit conversion out");
static_assert(!std::is_convertible<int, meters>::value, "no implicit conversion in");
static_assert(!std::is_convertible<meters, other>::value, "distinct tags are distinct types");
static_assert(std::is_same<strong::underlying_type_t<meters>, int>::value, "underlying_type_t");
static_assert(strong::type_is_v<meters, strong::arithmetic> && !strong::type_is_v<other, strong::arithmetic>,
              "type_is_v");
static_assert(!std::is_default_constructible<meters>::value && std::is_default_constructible<ratio>::value,
              "default construction only with regular");
static_assert(std::numeric_limits<meters>::is_specialized && !std::numeric_limits<other>::is_specialized,
              "numeric_limits only for arithmetic types");

static int failures = 0;

static void check(bool pass, const char *what) {
  if (!pass) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

static void test_arithmetic() {
  const meters a{17}, b{5};
  check(value_of(a + b) == 22 && value_of(a - b) == 12 && value_of(a * b) == 85, "+, -, *");
  check(value_of(a / b) == 3 && value_of(a % b) == 2 && value_of(-a) == -17, "/, %, unary -");
  check(b < a && a > b && a <= meters{17} && a == meters{17} && a != b, "ordering and equality");
  check(std::numeric_limits<meters>::max() == meters{std::numeric_limits<int>::max()},
        "numeric_limits is specialized for arithmetic types");

  meters c{1};
  ++c;
  c++;
  --c;
  check(value_of(c) == 2, "++ and --");

  std::ostringstream out;
  out << a << ' ' << b;
  check(out.str() == "17 5", "operator<<");
  std::istringstream in("42");
  meters read{0};
  in >> read;
  check(read == meters{42}, "operator>>");

  std::unordered_set<meters> seen;
  seen.insert(a);
  seen.insert(meters{17});
  seen.insert(b);
  check(seen.size() == 2 && seen.count(meters{5}) == 1, "std::hash");
  check(std::hash<meters>{}(a) == std::hash<int>{}(17), "the hash forwards to the underlying type");
}

static void test_other_modifiers() {
  const flags f{0x0Cu}, g{0x0Au};
  check((f & g) == flags{0x08u} && (f | g) == flags{0x0Eu} && (f ^ g) == flags{0x06u}, "&, |, ^");
  check((f << 2) == flags{0x30u} && (f >> 2) == flags{0x03u}, "shifts");
  check(static_cast<bool>(f) && !static_cast<bool>(flags{0u}), "explicit operator bool");

  const ratio r{3};
  check(static_cast<double>(r) == 3.0, "explicit conversion to double");
  check(r == 3 && 3 == r && r != 4 && r < 4 && 2 < r, "comparisons with int");
  ratio d;
  d = r;
  check(d == 3, "regular: default construction and assignment");

  const timestamp t0{1000}, t1{1250};
  const timestamp::difference elapsed = t1 - t0;
  check(value_of(elapsed) == 250 && t0 + elapsed == t1 && t1 - elapsed == t0, "affine point and its difference");

  names n{std::vector<std::string>{"ada", "grace", "edsger"}};
  std::string joined;
  for (const std::string &s : n) joined += s + ";";
  check(joined == "ada;grace;edsger;" && n.size() == 3, "range iteration and size");
  check(n[1] == "grace", "indexed<> subscript");
  check(std::distance(n.begin(), n.end()) == 3, "the range's iterators");
}

int main() {
  test_arithmetic();
  test_other_modifiers();
  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("strong_type 16.0.0 C++ ok");
  return 0;
}
