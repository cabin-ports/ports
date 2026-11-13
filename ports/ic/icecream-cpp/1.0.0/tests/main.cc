// Consumer test for cabin-ports/icecream-cpp: argument names and values,
// return-value forwarding, IC_A, containers, tuples, strings, user types,
// format specifiers, prefixes, and disabling.  Output goes to a string, and
// most expected lines follow upstream's tests/test_c++11.cpp and
// tests/test_strings_c++11.cpp.
#include <icecream.hpp>

#include <cstdio>
#include <list>
#include <ostream>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

static int failures = 0;

static void check(bool pass, const char *what) {
  if (!pass) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

static int sum(int a, int b) { return a + b; }

struct Point {
  int x, y;
};

static std::ostream &operator<<(std::ostream &os, Point const &p) {
  return os << "<Point " << p.x << ", " << p.y << ">";
}

static void test_names_and_values() {
  IC_CONFIG_SCOPE();
  std::string str;
  IC_CONFIG.output(str);

  int i0 = 7;
  double d0 = 3.14;
  IC(i0, d0);
  check(str == "ic| i0: 7, d0: 3.14\n", "IC prints each argument's text and value");

  str.clear();
  IC(sum(40, 2), i0, sum(3, 5));
  check(str == "ic| sum(40, 2): 42, i0: 7, sum(3, 5): 8\n", "IC prints expressions as written");

  str.clear();
  IC(std::is_same<int, int>::value);
  check(str == "ic| std::is_same<int, int>::value: true\n", "IC prints bool as true/false");

  str.clear();
  IC_CONFIG.disable();
  IC(i0);
  check(str.empty(), "a disabled config prints nothing");
}

static void test_return_values() {
  IC_CONFIG_SCOPE();
  std::string str;
  IC_CONFIG.output(str);

  check(std::is_same<decltype(IC(7)), int &&>::value, "IC(rvalue) forwards an rvalue reference");
  char c = 'r';
  char &r = IC(c);
  check(&r == &c, "IC(lvalue) returns the same object");

  int const a = 20;
  check((std::is_same<decltype(IC(7, 3.14, a)), std::tuple<int &&, double &&, int const &>>::value),
        "IC with several arguments returns a tuple of references");

  // IC_A gets named variables: it would read a temporary after its lifetime
  // ends (see cabin.toml).  The label is still the macro's name.
  str.clear();
  int v0 = 5, v1 = 10;
#define V1 v1
  int sum_v = IC_A(sum, v0, V1);
#undef V1
  check(sum_v == 15 && str == "ic| v0: 5, V1: 10\n", "IC_A prints the arguments and returns the call's result");
}

static void test_values() {
  IC_CONFIG_SCOPE();
  std::string str;
  IC_CONFIG.output(str);

  std::list<int> l{10, 20, 30};
  IC(l);
  check(str == "ic| l: [10, 20, 30]\n", "a list prints as a range");

  str.clear();
  std::vector<std::tuple<int, double>> vt;
  vt.push_back(std::make_tuple(1, 1.1));
  vt.push_back(std::make_tuple(2, 1.2));
  IC(vt);
  check(str == "ic| vt: [(1, 1.1), (2, 1.2)]\n", "a vector of tuples");

  str.clear();
  std::pair<int, char> p(5, 'a');
  std::tuple<int, double, char, char const *> t = std::make_tuple(1, 2.2, 'b', "bla");
  IC(p, t);
  check(str == "ic| p: (5, 'a'), t: (1, 2.2, 'b', \"bla\")\n", "pairs and tuples");

  str.clear();
  std::string s("str 1");
  IC(s);
  check(str == "ic| s: \"str 1\"\n", "a std::string prints quoted");

  str.clear();
  Point pt = {1, 2};
  IC(pt);
  check(str == "ic| pt: <Point 1, 2>\n", "a user type prints through its operator<<");
}

static void test_formatting() {
  IC_CONFIG_SCOPE();
  std::string str;
  IC_CONFIG.output(str);

  int v0 = 42;
  IC_F("#o", v0, 7);
  check(str == "ic| v0: 052, 7: 07\n", "#o formats in octal with a prefix");

  str.clear();
  IC_F("#b", v0, 7);
  check(str == "ic| v0: 0b101010, 7: 0b111\n", "#b formats in binary with a prefix");

  str.clear();
  IC_F("x", v0, IC_("*<", 5, "d", v0));
  check(str == "ic| v0: 2a, v0: 42***\n", "IC_ formats one argument with fill and width");

  str.clear();
  std::list<int> l{10, 20, 30};
  IC_F(":#x", l);
  check(str == "ic| l: [0xa, 0x14, 0x1e]\n", "a range format applies to every element");

  str.clear();
  int a = 10, b = 20;
  check(IC_FA("#x", sum, a, b) == 30 && str == "ic| a: 0xa, b: 0x14\n", "IC_FA formats the call's arguments");

  str.clear();
  IC_F("oA", v0);
  check(str == "ic| v0: *Error* in formatting string\n", "an invalid format string is reported");
}

static void test_prefix() {
  IC_CONFIG_SCOPE();
  std::string str;
  IC_CONFIG.output(str);

  IC_CONFIG.prefix("pref -> ");
  IC(1, 2);
  check(str == "pref -> 1: 1, 2: 2\n", "a string prefix");

  str.clear();
  IC_CONFIG.prefix("ic ", [] { return 5; }, "| ");
  IC(1, 2);
  check(str == "ic 5| 1: 1, 2: 2\n", "a prefix built from strings and a callable");
}

int main() {
  test_names_and_values();
  test_return_values();
  test_values();
  test_formatting();
  test_prefix();
  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("icecream-cpp 1.0.0 C++ ok");
  return 0;
}
