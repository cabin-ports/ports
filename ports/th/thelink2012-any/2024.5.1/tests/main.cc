#include <any.hpp>

#include <cstdio>
#include <initializer_list>
#include <string>
#include <typeinfo>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char* what) {
  if (!ok) {
    std::printf("FAIL: %s\n", what);
    ++failures;
  }
}

struct big_type {
  char data[256];
};

}  // namespace

int main() {
  // Value casts.
  linb::any a(42);
  check(!a.empty(), "int any is not empty");
  check(a.type() == typeid(int), "int any type");
  check(linb::any_cast<int>(a) == 42, "any_cast<int> value");

  linb::any s(std::string("cabin"));
  check(linb::any_cast<std::string>(s) == "cabin", "any_cast<std::string> value");

  // Pointer casts.
  check(linb::any_cast<int>(&s) == nullptr, "pointer cast to wrong type is null");
  const std::string* sp = linb::any_cast<std::string>(&s);
  check(sp != nullptr && *sp == "cabin", "pointer cast to right type");
  check(linb::any_cast<int>(static_cast<linb::any*>(nullptr)) == nullptr,
        "pointer cast of null any is null");

  // bad_any_cast.
  bool threw = false;
  try {
    linb::any_cast<int>(s);
  } catch (const linb::bad_any_cast& e) {
    threw = std::string(e.what()) == "bad any cast";
  }
  check(threw, "bad_any_cast with its what()");

  // swap: member and free function found by ADL.
  a.swap(s);
  check(linb::any_cast<std::string>(a) == "cabin" && linb::any_cast<int>(s) == 42,
        "member swap");
  swap(a, s);
  check(linb::any_cast<int>(a) == 42 && linb::any_cast<std::string>(s) == "cabin",
        "free swap");

  // clear.
  a.clear();
  check(a.empty(), "clear empties");
  check(a.type() == typeid(void), "empty any has typeid(void)");

  // A 256-byte type is stored on the heap and must copy intact.
  big_type big;
  for (int i = 0; i < 256; ++i) big.data[i] = static_cast<char>(i);
  linb::any heavy(big);
  linb::any copy(heavy);
  bool intact = true;
  const big_type& got = linb::any_cast<const big_type&>(copy);
  for (int i = 0; i < 256; ++i) intact = intact && got.data[i] == static_cast<char>(i);
  check(intact, "big type copies intact");

  // In-place construction (any.hpp in_place_type_t, make_any).
  linb::any made = linb::make_any<std::string>(3, 'x');
  check(linb::any_cast<std::string>(made) == "xxx", "make_any<std::string>(3, 'x')");
  linb::any listed = linb::make_any<std::vector<int>>({1, 2, 3});
  check(linb::any_cast<std::vector<int>&>(listed).size() == 3, "make_any with initializer_list");
  linb::any inplace(linb::in_place_type_t<int>{}, 7);
  check(linb::any_cast<int>(inplace) == 7, "in_place_type_t constructor");

  if (failures == 0) std::printf("OK\n");
  return failures == 0 ? 0 : 1;
}
