// Consumer test for cabin-ports/tuplet: element access, structured bindings,
// apply, tuple_cat, comparisons, tie, forward_as_tuple and convert, and the
// aggregate and trivially-copyable properties, checked against exact values.
#include <tuplet/tuple.hpp>

#include <array>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

using namespace tuplet::literals;

// Unlike std::tuple: an aggregate, and trivially copyable for trivial elements.
static_assert(std::is_aggregate_v<tuplet::tuple<int, double>>, "an aggregate");
static_assert(std::is_trivially_copyable_v<tuplet::tuple<int, double, char>>, "trivially copyable for trivial elements");
static_assert(!std::is_trivially_copyable_v<tuplet::tuple<int, std::string>>, "not for a non-trivial element");
static_assert(sizeof(tuplet::tuple<int, int>) == 2 * sizeof(int), "no storage beyond the elements");

constexpr tuplet::tuple<int, long> constant{4, 5L};
static_assert(tuplet::get<0>(constant) == 4 && constant[1_tag] == 5L, "usable in constant expressions");

static int failures = 0;

static void check(bool pass, const char *what) {
  if (!pass) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

struct Point {
  int x;
  double y;
  std::string_view label;
};

struct Owner {
  std::unique_ptr<int> value;
  int count;
};

static void test_access_and_bindings() {
  tuplet::tuple<int, int, std::string> tup = {1, 2, "Hello"};
  check(tup[0_tag] == 1 && tuplet::get<1>(tup) == 2 && tup[2_tag] == "Hello", "access by tag and by get");

  auto &[a, b, c] = tup;
  b = 20;
  c += ", world";
  check(a == 1 && tup[1_tag] == 20 && tup[2_tag] == "Hello, world", "structured bindings refer to the elements");

  auto [x, y] = tuplet::tuple{3, 4.5};
  check(x == 3 && y == 4.5, "structured bindings by value, with deduced element types");

  int r = 0;
  tuplet::tuple refs = {std::ref(r), 3};
  static_assert(std::is_same_v<decltype(refs), tuplet::tuple<int &, int>>, "std::ref deduces a reference element");
  refs[0_tag] = 6;
  check(r == 6, "a deduced reference element refers to its argument");
}

static void test_apply_and_cat() {
  tuplet::tuple<int, int, std::string> tup = {3, 4, "sum="};
  std::string applied = tuplet::apply([](int a, int b, const std::string &s) { return s + std::to_string(a + b); }, tup);
  check(applied == "sum=7", "apply passes the elements in order");

  auto cat = tuplet::tuple_cat(tuplet::tuple{1, 2}, tuplet::tuple<std::string>{"x"}, tuplet::tuple<>{}, tuplet::tuple{3.5});
  static_assert(std::is_same_v<decltype(cat), tuplet::tuple<int, int, std::string, double>>, "tuple_cat's type");
  check(cat[0_tag] == 1 && cat[1_tag] == 2 && cat[2_tag] == "x" && cat[3_tag] == 3.5, "tuple_cat's values");
}

static void test_comparisons() {
  tuplet::tuple<int, int> a{1, 2}, b{1, 3}, c{2, 0};
  check(a == tuplet::tuple<int, int>{1, 2} && a != b, "== and !=");
  check(a < b && b < c && a <= a && !(c < a), "< and <= compare lexicographically");
  check(c > b && b >= a && !(a > a), "> and >=");
  check(a == tuplet::tuple<long, long>{1L, 2L} && a < tuplet::tuple<long, long>{1L, 5L}, "heterogeneous comparison");
}

static void test_tie_and_forward() {
  int p = 0;
  std::string s;
  tuplet::tuple t = tuplet::tie(p, s);
  static_assert(std::is_same_v<decltype(t), tuplet::tuple<int &, std::string &>>, "tie holds references");
  t = tuplet::tuple{8, "eight"};
  check(p == 8 && s == "eight", "assigning through tie writes the variables");

  int n = 1;
  std::string tmp = "tmp";
  auto fwd = tuplet::forward_as_tuple(n, std::move(tmp));
  static_assert(std::is_same_v<decltype(fwd), tuplet::tuple<int &, std::string &&>>, "forward_as_tuple keeps value categories");
  fwd[0_tag] = 5;
  check(n == 5, "forward_as_tuple refers to its lvalue argument");
#if defined(__clang__) || !defined(__GNUC__) || __GNUC__ >= 16
  // GCC 13 rejects this access (cabin.toml); GCC 16 accepts it.
  std::string moved = static_cast<std::string &&>(fwd[1_tag]);
  check(moved == "tmp" && &fwd[1_tag] == &tmp, "forward_as_tuple refers to its rvalue argument");
#endif
}

static void test_convert() {
  auto tup = tuplet::tuple{1, 0.25, std::string_view("pt")};
  Point point = tuplet::convert{tup};
  check(point.x == 1 && point.y == 0.25 && point.label == "pt", "convert to an aggregate");

  std::array<int, 3> array = tuplet::convert{tuplet::tuple{7, 8, 9}};
  check(array[0] == 7 && array[1] == 8 && array[2] == 9, "convert to std::array");

  tuplet::tuple<std::unique_ptr<int>, int> owned{std::make_unique<int>(42), 3};
  Owner owner = tuplet::convert{std::move(owned)};
  check(owner.value && *owner.value == 42 && owner.count == 3 && !owned[0_tag], "convert moves from a moved tuple");
}

int main() {
  test_access_and_bindings();
  test_apply_and_cat();
  test_comparisons();
  test_tie_and_forward();
  test_convert();
  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("tuplet 2.1.1 C++ ok");
  return 0;
}
