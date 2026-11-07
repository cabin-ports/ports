// Builds frozen's containers and searchers at compile time, then checks
// them at run time with keys the compiler cannot see; checks the
// generators against [rand.predef] and std::minstd_rand0.
#include <frozen/algorithm.h>
#include <frozen/map.h>
#include <frozen/random.h>
#include <frozen/set.h>
#include <frozen/string.h>
#include <frozen/unordered_map.h>
#include <frozen/unordered_set.h>

#include <cstdio>
#include <cstring>
#include <random>
#include <stdexcept>
#include <string>

namespace {

int failures = 0;

void check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

// Keys the compiler cannot see, so lookups run the hashing and searching
// code rather than folding to a constant.
volatile int salt = 0;
frozen::string runtime(const char* s) {
  return frozen::string(s + salt, std::strlen(s));
}

constexpr frozen::set<int, 5> primes = {11, 2, 7, 3, 5};
static_assert(primes.size() == 5 && primes.count(7) == 1 &&
                  primes.count(4) == 0 && *primes.begin() == 2,
              "frozen::set at compile time");

constexpr frozen::map<frozen::string, int, 4> services = {
    {"https", 443}, {"ssh", 22}, {"http", 80}, {"dns", 53}};
static_assert(services.at("ssh") == 22, "frozen::map at compile time");

constexpr frozen::unordered_map<frozen::string, int, 12> months = {
    {"jan", 1}, {"feb", 2}, {"mar", 3}, {"apr", 4},  {"may", 5},  {"jun", 6},
    {"jul", 7}, {"aug", 8}, {"sep", 9}, {"oct", 10}, {"nov", 11}, {"dec", 12}};
static_assert(months.at("mar") == 3 && months.count("foo") == 0,
              "frozen::unordered_map at compile time");

constexpr frozen::unordered_set<frozen::string, 3> letters = {"a", "e", "y"};
static_assert(letters.count("y") == 1, "frozen::unordered_set");

constexpr auto evens = frozen::make_set<int>({6, 0, 4, 2});
static_assert(evens.count(4) == 1 && evens.count(3) == 0, "make_set");

constexpr auto kmp = frozen::make_knuth_morris_pratt_searcher("frozen");
constexpr auto bm = frozen::make_boyer_moore_searcher("frozen");
// A needle whose prefix recurs, so both searchers' shift tables matter.
constexpr auto kmp_abd = frozen::make_knuth_morris_pratt_searcher("ABCDABD");
constexpr auto bm_abd = frozen::make_boyer_moore_searcher("ABCDABD");

// Not constexpr: the keys stay fixed, the values can change.
frozen::unordered_map<frozen::string, int, 2> counters = {{"hits", 0},
                                                          {"misses", 0}};

void ordered() {
  const int sorted[] = {2, 3, 5, 7, 11};
  int i = 0;
  for (int p : primes) check(i < 5 && p == sorted[i++], "set order");
  int seven = 7 + salt;
  check(*primes.lower_bound(seven) == 7 && *primes.upper_bound(seven) == 11,
        "set bounds of a member");
  // Unlike std::set, frozen::set's lower_bound of a non-member is end()
  // (set.h:114-121; upstream's tests/test_set.cpp:89-96 pin it for keys
  // outside the range); frozen::map's is std::map's.
  check(primes.lower_bound(seven - 6) == primes.end() &&
            primes.lower_bound(seven - 1) == primes.end(),
        "set lower_bound of a non-member");
  check(primes.find(seven - 3) == primes.end(), "set find of a non-member");
  check(primes.count(seven + 4) == 1, "set count");

  const char* keys[] = {"dns", "http", "https", "ssh"};
  i = 0;
  for (const auto& kv : services) {
    check(i < 4 && std::string(kv.first.data(), kv.first.size()) == keys[i],
          "map order");
    ++i;
  }
  check(services.at(runtime("https")) == 443, "map at");
  check(services.lower_bound(runtime("dot"))->second == 80 &&
            services.upper_bound(runtime("https"))->second == 22,
        "map bounds");
  check(services.find(runtime("ftp")) == services.end(),
        "map find of a non-member");
  bool threw = false;
  try {
    services.at(runtime("ftp"));
  } catch (const std::out_of_range&) {
    threw = true;
  }
  check(threw, "map at of a non-member throws std::out_of_range");
}

void unordered() {
  const char* names[] = {"jan", "feb", "mar", "apr", "may", "jun",
                         "jul", "aug", "sep", "oct", "nov", "dec"};
  for (int m = 0; m < 12; ++m) {
    auto it = months.find(runtime(names[m]));
    check(it != months.end() && it->second == m + 1, "unordered_map find");
  }
  check(months.size() == 12, "unordered_map size");
  check(months.count(runtime("Jan")) == 0, "unordered_map is case-sensitive");
  bool threw = false;
  try {
    months.at(runtime("smarch"));
  } catch (const std::out_of_range&) {
    threw = true;
  }
  check(threw, "unordered_map at of a non-member throws std::out_of_range");
  check(letters.count(runtime("e")) == 1 && letters.count(runtime("i")) == 0,
        "unordered_set count");

  counters.at(runtime("hits")) = 3;
  ++counters.find(runtime("misses"))->second;
  ++counters.at(runtime("hits"));
  check(counters.at("hits") == 4 && counters.at("misses") == 1,
        "mutable values");
}

void searchers() {
  // A near miss ("froze,") before the first of two matches.
  std::string text = "it froze, then frozen solid, frozen again";
  std::size_t want = text.find("frozen");
  auto k = frozen::search(text.begin(), text.end(), kmp);
  auto b = frozen::search(text.begin(), text.end(), bm);
  check(static_cast<std::size_t>(k - text.begin()) == want,
        "Knuth-Morris-Pratt search");
  check(static_cast<std::size_t>(b - text.begin()) == want,
        "Boyer-Moore search");
  std::string abd = "ABC ABCDAB ABCDABCDABDE";  // upstream's test_str.cpp
  check(frozen::search(abd.begin(), abd.end(), kmp_abd) - abd.begin() == 15,
        "Knuth-Morris-Pratt search with a self-overlapping needle");
  check(frozen::search(abd.begin(), abd.end(), bm_abd) - abd.begin() == 15,
        "Boyer-Moore search with a self-overlapping needle");
  std::string thawed = "frozeN frozzen rozen";
  check(frozen::search(thawed.begin(), thawed.end(), kmp) == thawed.end(),
        "Knuth-Morris-Pratt miss");
  check(frozen::search(thawed.begin(), thawed.end(), bm) == thawed.end(),
        "Boyer-Moore miss");
}

void generators() {
  // [rand.predef]: the 10000th invocation of a default-constructed
  // minstd_rand yields 399268537.
  frozen::minstd_rand g;
  g.discard(9999 + salt);
  check(g() == 399268537u, "minstd_rand 10000th value");
  frozen::minstd_rand0 f(12345u + salt);
  std::minstd_rand0 s(12345u);
  bool same = true;
  for (int i = 0; i < 1000; ++i) same = same && f() == s();
  check(same, "minstd_rand0 matches std::minstd_rand0");
}

}  // namespace

int main() {
  ordered();
  unordered();
  searchers();
  generators();
  if (failures != 0) return 1;
  std::puts("frozen consumer ok");
  return 0;
}
