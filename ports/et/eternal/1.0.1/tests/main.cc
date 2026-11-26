// Builds sorted and hashed maps at compile time and checks lookups, bounds,
// duplicate keys and iteration order against values worked out by hand.
#include <mapbox/eternal.hpp>

#include <cstdio>
#include <cstring>

namespace {

namespace eternal = mapbox::eternal;

struct Color {
  int r, g, b;
  constexpr bool operator==(const Color& o) const {
    return r == o.r && g == o.g && b == o.b;
  }
};

constexpr auto colors = eternal::map<eternal::string, Color>({
    {"red", {255, 0, 0}},
    {"green", {0, 128, 0}},
    {"black", {0, 0, 0}},
});
static_assert(colors.unique(), "");
static_assert(colors.size() == 3, "");
static_assert(colors.at("green").g == 128, "");
static_assert(colors.find("red")->second == Color{255, 0, 0}, "");
static_assert(colors.find("blue") == colors.end(), "");

constexpr auto hashed = eternal::hash_map<eternal::string, int>({
    {"a", 1},
    {"b", 2},
    {"c", 3},
});
static_assert(hashed.size() == 3 && hashed.find("b")->second == 2, "");
static_assert(hashed.find("z") == hashed.end(), "");

constexpr auto dup = eternal::map<int, char>({
    {5, 'x'}, {1, 'a'}, {5, 'y'}, {9, 'z'}, {5, 'w'},
});
static_assert(!dup.unique(), "");

bool ok = true;

void check(bool pass, const char* what) {
  if (!pass) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ok = false;
  }
}

}  // namespace

int main() {
  // Keys are sorted at compile time: black < green < red.
  auto it = colors.begin();
  check(std::strcmp(it->first.c_str(), "black") == 0, "first key is black");
  check((++it)->second == Color{0, 128, 0}, "second is green");
  check(std::strcmp((++it)->first.data(), "red") == 0, "third key is red");
  check(++it == colors.end(), "iteration ends after three");

  check(colors.contains("black") && !colors.contains("Black"), "contains");
  check(colors.count("red") == 1 && colors.count("blue") == 0, "count");
  check(std::strcmp(colors.lower_bound("h")->first.c_str(), "red") == 0,
        "lower_bound of a missing key is the next larger key");
  check(colors.upper_bound("red") == colors.end(), "upper_bound past last");

  // Hashed lookups agree with the stored values for a runtime key.
  const char* runtime_key = "c";
  check(hashed.at(runtime_key) == 3, "hash_map runtime lookup");
  check(!hashed.contains("d"), "hash_map miss");

  // Three entries share key 5: equal_range spans exactly those.
  auto range = dup.equal_range(5);
  check(range.second - range.first == 3, "equal_range width");
  check(dup.count(5) == 3 && dup.count(1) == 1 && dup.count(7) == 0,
        "duplicate counts");
  check(dup.lower_bound(5) == range.first && dup.upper_bound(5) == range.second,
        "bounds match equal_range");
  auto last = dup.end();
  --last;
  check(dup.begin()->second == 'a' && last->second == 'z', "int keys sorted");

  if (ok) std::puts("ok");
  return ok ? 0 : 1;
}
