#include <cstdio>
#include <cstdlib>
#include <string>

#include "semimap.h"

#define ID(x) []() constexpr { return x; }
#define CHECK(c)                                                  \
  do {                                                            \
    if (!(c)) {                                                   \
      std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); \
      std::exit(1);                                               \
    }                                                             \
  } while (0)

struct Tag {};
using S = semi::static_map<std::string, std::string, Tag>;

int main() {
  // static_map: compile-time keys, default-constructed missing values
  CHECK(S::get(ID("food")).empty());
  S::get(ID("food")) = "pizza";
  S::get(ID("drink")) = "soda";
  CHECK(S::get(ID("food")) == "pizza");
  CHECK(S::get(ID("drink")) == "soda");

  // run-time keys reach the same storage as the literal keys
  std::string k = "drink";
  CHECK(S::get(k) == "soda");
  S::get(k) = "beer";
  CHECK(S::get(ID("drink")) == "beer");
  S::get("side") = "rice";
  CHECK(S::get(ID("side")) == "rice");

  // extra arguments construct only a missing value
  CHECK(S::get(ID("starter"), "soup") == "soup");
  CHECK(S::get(ID("starter"), "salad") == "soup");

  // contains does not insert; erase removes from both lookup paths
  CHECK(!S::contains(ID("dessert")));
  CHECK(!S::contains("dessert"));
  CHECK(S::contains(ID("food")) && S::contains("food"));
  S::erase(ID("food"));
  CHECK(!S::contains(ID("food")) && !S::contains("food"));
  CHECK(S::contains("drink") && S::contains(ID("starter")));
  S::clear();
  CHECK(!S::contains("drink") && !S::contains(ID("starter")));

  // different tags are independent maps
  struct Other {};
  using O = semi::static_map<std::string, int, Other>;
  O::get(ID("age")) = 18;
  CHECK(O::get("age") == 18);
  CHECK(O::get(ID("missing")) == 0);

  // semi::map: per-instance storage
  semi::map<std::string, std::string> a, b;
  a.get(ID("food")) = "pizza";
  b.get("food") = "spaghetti";
  CHECK(a.get("food") == "pizza" && b.get(ID("food")) == "spaghetti");
  CHECK(!a.contains("drink") && a.get(ID("drink")).empty());
  CHECK(a.contains(ID("drink")));
  a.erase("food");
  CHECK(!a.contains(ID("food")) && b.contains("food"));
  b.clear();
  CHECK(!b.contains("food"));
  return 0;
}
