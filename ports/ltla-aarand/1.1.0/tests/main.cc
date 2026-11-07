// Consumer test for cabin-ports/ltla-aarand: checks the transforms exactly
// with a hand-written engine whose outputs are known, and every public
// function with std::mt19937_64 for distribution-level checks.
#include <aarand/aarand.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <iterator>
#include <map>
#include <numeric>
#include <random>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAILED: %s\n", what);
    ++failures;
  }
}

bool near(double a, double b, double tol) { return std::fabs(a - b) <= tol; }

template <class Fn>
bool throws_runtime(Fn fn, const char* message) {
  try {
    fn();
  } catch (const std::runtime_error& e) {
    return std::string(e.what()) == message;
  }
  return false;
}

// Yields 100, 101, ..., 109, 100, ...: ten known values above a non-zero
// min(), which aarand subtracts before scaling.
struct Counter {
  using result_type = std::uint32_t;
  static constexpr result_type min() { return 100; }
  static constexpr result_type max() { return 109; }
  result_type next = 0;
  result_type operator()() {
    const result_type out = min() + next;
    next = (next + 1) % 10;
    return out;
  }
};

void test_uniform() {
  Counter c;
  bool exact = true;
  for (int k = 0; k < 10; ++k) {
    exact = exact && near(aarand::standard_uniform(c), k / 10.0, 1e-12);
  }
  check(exact, "standard_uniform maps a draw to (draw - min()) / (range + 1)");
  static_assert(
      std::is_same<decltype(aarand::standard_uniform<float>(c)), float>::value,
      "Output_ selects the result type");

  std::mt19937_64 rng(42);
  const int n = 100000;
  double sum = 0, sum2 = 0;
  std::vector<int> bins(10, 0);
  bool in_range = true;
  for (int i = 0; i < n; ++i) {
    const double u = aarand::standard_uniform(rng);
    if (u < 0 || u >= 1) {
      in_range = false;
      continue;
    }
    sum += u;
    sum2 += u * u;
    ++bins[static_cast<std::size_t>(u * 10)];
  }
  const double mean = sum / n;
  check(in_range, "standard_uniform lies in [0, 1)");
  check(near(mean, 0.5, 0.01), "standard_uniform mean is 1/2");
  check(near(sum2 / n - mean * mean, 1.0 / 12, 0.005),
        "standard_uniform variance is 1/12");
  bool flat = true;
  for (int b : bins) flat = flat && b > 9500 && b < 10500;
  check(flat, "standard_uniform fills ten bins evenly");
}

// The point of aarand: given an engine the standard fully specifies, every
// standard library yields the same draws.  The 10000th output of a
// default-constructed std::mt19937_64 is 9981545732273789042 ([rand.predef]).
void test_reproducible() {
  std::mt19937_64 rng;
  rng.discard(9999);
  std::mt19937_64 copy = rng;
  check(aarand::standard_uniform(rng) ==
            std::ldexp(static_cast<double>(9981545732273789042ULL), -64),
        "standard_uniform of a pinned draw is draw * 2^-64");
  check(aarand::discrete_uniform(copy, 1000) == 42,
        "discrete_uniform of a pinned draw is draw % bound");

  std::mt19937_64 a(123), b(123);
  bool same = true;
  for (int i = 0; i < 100; ++i) {
    same = same && aarand::standard_normal(a) == aarand::standard_normal(b) &&
           aarand::discrete_uniform(a, 77) == aarand::discrete_uniform(b, 77);
  }
  check(same, "equal seeds give equal sequences");
}

void test_discrete() {
  // Range 9, bound 3: draws 0..6 map straight to draw % 3, 7 and 8 pass the
  // rejection limit 9 - (9 % 3 + 1) = 8, and 9 is rejected and redrawn, so
  // no residue gains the extra draw.
  Counter c;
  std::vector<int> got;
  for (int i = 0; i < 11; ++i) got.push_back(aarand::discrete_uniform(c, 3));
  check(got == std::vector<int>{0, 1, 2, 0, 1, 2, 0, 1, 2, 0, 1},
        "discrete_uniform rejects the draw above the limit");
  static_assert(std::is_same<decltype(aarand::discrete_uniform(
                                 c, std::size_t(3))),
                             std::size_t>::value,
                "the bound's type is the result type");

  check(throws_runtime([&] { aarand::discrete_uniform(c, 0); },
                       "'bound' should be a positive integer"),
        "discrete_uniform rejects a zero bound");
  check(throws_runtime([&] { aarand::discrete_uniform(c, -5); },
                       "'bound' should be a positive integer"),
        "discrete_uniform rejects a negative bound");
  check(throws_runtime([&] { aarand::discrete_uniform(c, 10); },
                       "'bound' should be less than the RNG range"),
        "discrete_uniform rejects a bound above max() - min()");
  check(aarand::discrete_uniform(c, 9) == 2,
        "discrete_uniform accepts a bound of max() - min(), and the "
        "rejected bounds drew nothing");

  std::mt19937 rng32(5);
  const std::int64_t big = std::int64_t(1) << 33;
  check(throws_runtime([&] { aarand::discrete_uniform(rng32, big); },
                       "'bound' should be less than the RNG range"),
        "a 32-bit engine cannot draw below 2^33");
  const std::int64_t ok = std::int64_t(1) << 31;
  const std::int64_t draw = aarand::discrete_uniform(rng32, ok);
  check(draw >= 0 && draw < ok, "a 32-bit engine draws below 2^31");

  std::mt19937_64 rng(7);
  std::vector<int> counts(6, 0);
  bool in_range = true;
  for (int i = 0; i < 60000; ++i) {
    const int d = aarand::discrete_uniform(rng, 6);
    in_range = in_range && d >= 0 && d < 6;
    if (in_range) ++counts[d];
  }
  check(in_range, "discrete_uniform lies in [0, bound)");
  bool flat = true;
  for (int n : counts) flat = flat && n > 9500 && n < 10500;
  check(flat, "discrete_uniform hits every value evenly");
}

void test_exponential() {
  // The first draw is 0, so the uniform is rerolled before the log.
  Counter c;
  check(near(aarand::standard_exponential(c), 2.3025850929940455, 1e-12),
        "standard_exponential rerolls 0 and returns -log(0.1)");
  check(near(aarand::standard_exponential(c), 1.6094379124341003, 1e-12),
        "standard_exponential returns -log(0.2)");

  std::mt19937_64 rng(11);
  const int n = 100000;
  double sum = 0, sum2 = 0;
  bool nonneg = true;
  for (int i = 0; i < n; ++i) {
    const double e = aarand::standard_exponential(rng);
    nonneg = nonneg && e >= 0 && std::isfinite(e);
    sum += e;
    sum2 += e * e;
  }
  const double mean = sum / n;
  check(nonneg, "standard_exponential is finite and non-negative");
  check(near(mean, 1, 0.02), "standard_exponential mean is 1");
  check(near(sum2 / n - mean * mean, 1, 0.05),
        "standard_exponential variance is 1");
}

void test_normal() {
  // Box-Muller on u1 = 0.1 (after rerolling 0) and u2 = 0.2.
  Counter c;
  const std::pair<double, double> p = aarand::standard_normal(c);
  check(near(p.first, 2.0409349730505, 1e-9) &&
            near(p.second, 0.6631399714746835, 1e-9),
        "standard_normal is sqrt(-2 log u1) * (sin, cos)(2 pi u2)");
  static_assert(std::is_same<decltype(aarand::standard_normal<float>(c)),
                             std::pair<float, float>>::value,
                "Output_ selects the pair's type");

  std::mt19937_64 rng(13);
  const int n = 100000;
  double sum = 0, sum2 = 0, cross = 0;
  int within_one = 0;
  for (int i = 0; i < n; ++i) {
    const std::pair<double, double> z = aarand::standard_normal(rng);
    sum += z.first + z.second;
    sum2 += z.first * z.first + z.second * z.second;
    cross += z.first * z.second;
    within_one += (std::fabs(z.first) < 1) + (std::fabs(z.second) < 1);
  }
  const double mean = sum / (2.0 * n);
  check(near(mean, 0, 0.01), "standard_normal mean is 0");
  check(near(sum2 / (2.0 * n) - mean * mean, 1, 0.02),
        "standard_normal variance is 1");
  check(near(cross / n, 0, 0.02), "the pair is uncorrelated");
  check(near(within_one / (2.0 * n), 0.6827, 0.01),
        "68% of standard_normal lies within one sd");
}

void test_shuffle() {
  std::mt19937_64 rng(17);
  std::vector<int> v(20);
  std::iota(v.begin(), v.end(), 0);
  const std::vector<int> original = v;
  aarand::shuffle(v.begin(), v.size(), rng);
  check(std::is_permutation(v.begin(), v.end(), original.begin()),
        "shuffle permutes the values");
  check(v != original, "shuffle moves the values");

  int one[] = {7};
  const std::mt19937_64 before = rng;
  aarand::shuffle(one, 1, rng);
  aarand::shuffle(one, 0, rng);
  check(one[0] == 7 && rng == before,
        "shuffle of one or zero values draws nothing");

  // Fisher-Yates makes all 24 orders of four values equally likely.
  std::map<std::vector<int>, int> orders;
  for (int i = 0; i < 48000; ++i) {
    int w[] = {0, 1, 2, 3};
    aarand::shuffle(w, 4, rng);
    ++orders[std::vector<int>(w, w + 4)];
  }
  bool uniform = orders.size() == 24;
  for (const auto& entry : orders) {
    uniform = uniform && entry.second > 1700 && entry.second < 2300;
  }
  check(uniform, "shuffle gives every order of four values equally often");
}

void test_sample_values() {
  std::mt19937_64 rng(19);
  std::vector<int> values(20);
  std::iota(values.begin(), values.end(), 100);
  const std::size_t n = values.size();

  std::vector<int> picked(n, 0);
  bool ordered = true;
  for (int i = 0; i < 20000; ++i) {
    std::vector<int> out;
    aarand::sample(values.begin(), n, std::size_t(5), std::back_inserter(out),
                   rng);
    ordered = ordered && out.size() == 5 &&
              std::adjacent_find(out.begin(), out.end(),
                                 std::greater_equal<int>()) == out.end() &&
              out.front() >= 100 && out.back() < 120;
    if (ordered) {
      for (int x : out) ++picked[static_cast<std::size_t>(x - 100)];
    }
  }
  check(ordered, "sample picks s distinct values in input order");
  bool fair = true;
  for (int p : picked) fair = fair && p > 4500 && p < 5500;
  check(fair, "sample includes each value with probability s / n");

  std::vector<int> all(n, -1);
  aarand::sample(values.begin(), n, n, all.begin(), rng);
  check(all == values, "sample of s == n copies every value");
  std::vector<int> extra(n + 3, -1);
  aarand::sample(values.begin(), n, n + 3, extra.begin(), rng);
  check(std::equal(values.begin(), values.end(), extra.begin()),
        "sample of s > n copies the values first");

  std::vector<int> none;
  const std::mt19937_64 before = rng;
  aarand::sample(values.begin(), n, std::size_t(0), std::back_inserter(none),
                 rng);
  check(none.empty() && rng == before, "sample of zero values draws nothing");
}

void test_sample_indices() {
  std::mt19937_64 rng(23);
  std::vector<int> out(10, -1);
  aarand::sample(100, 10, out.begin(), rng);
  check(std::adjacent_find(out.begin(), out.end(),
                           std::greater_equal<int>()) == out.end() &&
            out.front() >= 0 && out.back() < 100,
        "sample of indices gives increasing indices below the bound");

  std::vector<int> picked(10, 0);
  bool fair = true;
  for (int i = 0; i < 30000; ++i) {
    int idx[3] = {-1, -1, -1};
    aarand::sample(10, 3, idx, rng);
    for (int x : idx) {
      if (x < 0 || x >= 10) fair = false;
      else ++picked[static_cast<std::size_t>(x)];
    }
  }
  for (int p : picked) fair = fair && p > 8550 && p < 9450;
  check(fair, "sample includes each index with probability s / bound");

  std::vector<std::size_t> all(6, 99);
  aarand::sample(std::size_t(6), std::size_t(6), all.begin(), rng);
  check(all == std::vector<std::size_t>{0, 1, 2, 3, 4, 5},
        "sample of every index is 0, ..., bound - 1");

  std::vector<int> untouched(3, -1);
  aarand::sample(5, 0, untouched.begin(), rng);
  check(untouched == std::vector<int>(3, -1),
        "sample of zero indices writes nothing");
}

}  // namespace

int main() {
  test_uniform();
  test_reproducible();
  test_discrete();
  test_exponential();
  test_normal();
  test_shuffle();
  test_sample_values();
  test_sample_indices();
  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("ltla-aarand consumer ok");
  return 0;
}
