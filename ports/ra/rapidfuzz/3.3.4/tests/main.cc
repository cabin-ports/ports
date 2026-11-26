#include <cmath>
#include <cstdio>
#include <string>

#include <rapidfuzz/distance.hpp>
#include <rapidfuzz/fuzz.hpp>

namespace fuzz = rapidfuzz::fuzz;

static int failures = 0;

static void check(bool ok, const char* what) {
  if (!ok) {
    std::printf("FAIL: %s\n", what);
    ++failures;
  }
}

static bool near(double a, double b) { return std::fabs(a - b) < 1e-3; }

int main() {
  const std::string a = "this is a test", b = "this is a test!";
  const std::string f1 = "fuzzy wuzzy was a bear", f2 = "wuzzy fuzzy was a bear";

  // Indel similarity: 1 insertion over 29 characters, 4 over 44 (hand-computed).
  check(near(fuzz::ratio(a, b), 100.0 * (1.0 - 1.0 / 29.0)), "ratio");
  check(near(fuzz::ratio(f1, f2), 100.0 * (1.0 - 4.0 / 44.0)), "ratio words");
  check(near(fuzz::partial_ratio(a, b), 100.0), "partial_ratio");
  check(near(fuzz::token_sort_ratio(f1, f2), 100.0), "token_sort_ratio");
  // "a bear fuzzy was" vs "a bear fuzzy fuzzy was": Indel distance 6 over 38.
  check(near(fuzz::token_sort_ratio(std::string("fuzzy was a bear"),
                                    std::string("fuzzy fuzzy was a bear")),
             100.0 * (1.0 - 6.0 / 38.0)),
        "token_sort_ratio duplicate");
  check(near(fuzz::token_set_ratio(std::string("fuzzy was a bear"),
                                   std::string("fuzzy fuzzy was a bear")),
             100.0),
        "token_set_ratio");

  // Cached scorer agrees with the free function; cutoff zeroes a lower score.
  fuzz::CachedRatio<char> cached(a);
  check(cached.similarity(b) == fuzz::ratio(a, b), "cached ratio");
  check(fuzz::ratio(a, b, 98.0) == 0.0, "ratio below cutoff");
  check(near(fuzz::ratio(a, b, 96.0), 100.0 * (1.0 - 1.0 / 29.0)),
        "ratio above cutoff");
  check(cached.similarity(b, 98.0) == 0.0, "cached below cutoff");

  // Levenshtein with unit and weighted {insert, delete, replace} costs.
  const std::string k = "kitten", s = "sitting";
  check(rapidfuzz::levenshtein_distance(k, s) == 3, "levenshtein");
  check(rapidfuzz::levenshtein_distance(std::string("lewenstein"),
                                        std::string("levenshtein")) == 2,
        "levenshtein lewenstein");
  check(rapidfuzz::levenshtein_distance(std::string("lewenstein"),
                                        std::string("levenshtein"),
                                        {1, 1, 2}) == 3,
        "levenshtein replace 2");
  check(rapidfuzz::levenshtein_distance(k, s, {1, 1, 2}) == 5, "indel-like");
  check(rapidfuzz::levenshtein_distance(k, s, {2, 1, 1}) == 4, "insert 2");
  check(rapidfuzz::levenshtein_distance(k, s, {1, 2, 1}) == 3, "delete 2");
  check(rapidfuzz::levenshtein_distance(s, k, {1, 2, 1}) == 4, "delete 2 back");
  check(near(rapidfuzz::levenshtein_normalized_similarity(k, s), 1.0 - 3.0 / 7.0),
        "normalized similarity");
  check(near(rapidfuzz::levenshtein_normalized_distance(k, s), 3.0 / 7.0),
        "normalized distance");
  // A distance above max is reported as max + 1; a similarity below cutoff as 0.
  check(rapidfuzz::levenshtein_distance(k, s, {1, 1, 1}, 2) == 3, "max");
  check(rapidfuzz::levenshtein_normalized_similarity(k, s, {1, 1, 1}, 0.8) == 0.0,
        "similarity cutoff");

  if (failures == 0) std::puts("ok");
  return failures != 0;
}
