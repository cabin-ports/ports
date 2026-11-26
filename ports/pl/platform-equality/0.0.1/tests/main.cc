#include <Platform.Equality.h>

#include <cstdlib>
#include <iostream>
#include <list>
#include <span>
#include <string>
#include <vector>

#define CHECK(c)                                                         \
  do {                                                                   \
    if (!(c)) {                                                          \
      std::cerr << "FAIL line " << __LINE__ << ": " #c << std::endl;     \
      std::exit(1);                                                      \
    }                                                                    \
  } while (0)

struct Nil {};

int main() {
  // std::any against std::any, built-in comparers.
  CHECK(std::any(1) == std::any(1));
  CHECK(!(std::any(1) == std::any(2)));
  CHECK(std::any(2.5) == std::any(2.5));
  CHECK(!(std::any(2.5) == std::any(2.75)));
  CHECK(std::any(std::string("abc")) == std::any(std::string("abc")));
  CHECK(!(std::any(std::string("abc")) == std::any(std::string("abd"))));
  // Different held types compare unequal, even when the values look alike.
  CHECK(!(std::any(1) == std::any(1.0f)));
  CHECK(!(std::any(std::string("a")) == std::any("a")));
  // Same const char* value compares by pointer (std::equal_to<const char*>).
  const char* s = "green";
  CHECK(std::any(s) == std::any(s));

  // std::any against a plain value, both operand orders.
  std::any n = 12;
  CHECK(n == 12);
  CHECK(12 == n);
  CHECK(!(n == 13));
  CHECK(!(13 == n));

  // A type without a registered comparer throws.
  bool threw = false;
  try {
    (void)(std::any(std::vector<int>{1}) == std::any(std::vector<int>{1}));
  } catch (const std::runtime_error&) {
    threw = true;
  }
  CHECK(threw);

  // A registered comparer decides equality.
  Platform::Equality::RegisterEqualityComparer<Nil>(
      [](const Nil&, const Nil&) { return true; });
  CHECK(std::any(Nil{}) == std::any(Nil{}));
  CHECK(std::any(Nil{}) == std::any(Nil{}) && !(std::any(1) == std::any(Nil{})));

  // Registering a type again keeps the first comparer.
  Platform::Equality::RegisterEqualityComparer<Nil>(
      [](const Nil&, const Nil&) { return false; });
  CHECK(std::any(Nil{}) == std::any(Nil{}));

  // Range comparison through std::equal_to for ranges without operator==.
  std::vector<int> a{1, 2, 3, 4, 5};
  std::vector<int> b{1, 2, 3, 4, 5};
  std::vector<int> c{5, 4, 3, 2, 1};
  std::vector<int> shorter{1, 2, 3};
  std::equal_to<std::span<int>> eq;
  CHECK(eq(std::span{a}, std::span{b}));
  CHECK(!eq(std::span{a}, std::span{c}));
  CHECK(!eq(std::span{a}, std::span{shorter}));
  std::equal_to<std::span<const int>> eqc;
  CHECK(eqc(std::span<const int>{a}, std::span<const int>{b}));

  Platform::Equality::EqualityComparerFunctionType<int> f = std::equal_to<int>{};
  CHECK(f(3, 3));
  CHECK(!f(3, 4));
  return 0;
}
