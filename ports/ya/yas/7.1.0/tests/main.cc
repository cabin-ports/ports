// Serializes with yas: README.md's JSON example, saved to exact text and
// loaded back, and a binary round trip of a vector and a string.  The JSON
// example uses a double: a float is written through double and would print
// 3.140000104904175.

#include <yas/serialize.hpp>
#include <yas/std_types.hpp>

#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

static int failures = 0;

static void check(bool ok, const char *what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

static void test_json() {
  int a = 3, aa{};
  short b = 4, bb{};
  double c = 3.14, cc{};

  constexpr std::size_t flags = yas::mem | yas::json;
  auto buf = yas::save<flags>(YAS_OBJECT("myobject", a, b, c));
  check(std::string(buf.data.get(), buf.size) == R"({"a":3,"b":4,"c":3.14})", "JSON text");

  yas::load<flags>(buf, YAS_OBJECT_NVP("myobject", ("a", aa), ("b", bb), ("c", cc)));
  check(aa == 3 && bb == 4 && cc == 3.14, "JSON load");
}

static void test_binary() {
  std::vector<int> v{1, -2, 300000}, vv;
  std::string s = "yas \"binary\"\n", ss;

  constexpr std::size_t flags = yas::mem | yas::binary;
  auto buf = yas::save<flags>(YAS_OBJECT("o", v, s));
  yas::load<flags>(buf, YAS_OBJECT_NVP("o", ("v", vv), ("s", ss)));
  check(vv == v, "binary vector round trip");
  check(ss == s, "binary string round trip");
}

int main() {
  test_json();
  test_binary();
  if (failures != 0) {
    return 1;
  }
  std::puts("yas 7.1.0 C++ ok");
  return 0;
}
