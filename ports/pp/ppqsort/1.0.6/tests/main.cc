#include <ppqsort.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char* what, int line) {
  if (!ok) {
    std::fprintf(stderr, "main.cc:%d: check failed: %s\n", line, what);
    ++failures;
  }
}

#define CHECK(...) check((__VA_ARGS__), #__VA_ARGS__, __LINE__)

// The parallel policies fall back to the sequential sort below 1 << 18
// elements (parameters::seq_threshold), so the parallel cases use more.
constexpr std::size_t big = std::size_t{1} << 20;

std::vector<int> random_ints(std::size_t n) {
  std::vector<int> v(n);
  std::uint32_t x = 1;
  for (auto& e : v) {
    x = x * 1103515245u + 12345u;
    e = static_cast<int>(x >> 8);
  }
  return v;
}

template <class Sort, class Compare = std::less<>>
bool sorts_like_std(std::vector<int> v, Sort sort, Compare comp = {}) {
  std::vector<int> expected = v;
  std::sort(expected.begin(), expected.end(), comp);
  sort(v);
  return v == expected;
}

void test_small() {
  std::vector<int> v{5, 3, 9, 1, 7, 3};
  ppqsort::sort(v.begin(), v.end());
  CHECK(v == std::vector<int>{1, 3, 3, 5, 7, 9});
  ppqsort::sort(v.begin(), v.end(), std::greater<int>());
  CHECK(v == std::vector<int>{9, 7, 5, 3, 3, 1});
  // An empty range returns before any thread pool starts.
  std::vector<int> empty;
  ppqsort::sort(ppqsort::execution::par, empty.begin(), empty.end());
}

// Every parallel sort aborts under libstdc++ assertions (upstream issue #10,
// the limitation in cabin.toml), so the parallel checks run only without them.
#if !defined(_GLIBCXX_ASSERTIONS)
constexpr bool run_parallel = true;
#else
constexpr bool run_parallel = false;
#endif

void test_policies() {
  const std::vector<int> input = random_ints(big);
  using namespace ppqsort::execution;
  CHECK(sorts_like_std(input, [](auto& v) { ppqsort::sort(v.begin(), v.end()); }));
  CHECK(sorts_like_std(input, [](auto& v) { ppqsort::sort(seq, v.begin(), v.end(), std::greater<int>()); },
                       std::greater<>()));
  // ints with std::less already take the branchless partition; a lambda comparator takes the branching one.
  auto const by_value = [](int a, int b) { return a < b; };
  CHECK(sorts_like_std(input, [&](auto& v) { ppqsort::sort(seq, v.begin(), v.end(), by_value); }));
  // One thread takes the sequential path even under a parallel policy.
  CHECK(sorts_like_std(input, [](auto& v) { ppqsort::sort(par, v.begin(), v.end(), 1); }));
  if (!run_parallel) return;
  CHECK(sorts_like_std(input, [](auto& v) { ppqsort::sort(par, v.begin(), v.end(), 4); }));
  CHECK(sorts_like_std(input, [](auto& v) { ppqsort::sort(par, v.begin(), v.end()); }));
  CHECK(sorts_like_std(input, [&](auto& v) { ppqsort::sort(par, v.begin(), v.end(), by_value, 3); }));
  CHECK(sorts_like_std(
      input, [](auto& v) { ppqsort::sort(par, v.begin(), v.end(), std::greater<int>(), 4); }, std::greater<>()));
}

// Sorted, reversed and constant inputs take the pattern-detecting paths.
void test_patterns() {
  std::vector<int> rising(big);
  for (std::size_t i = 0; i < big; ++i) rising[i] = static_cast<int>(i / 3);
  const std::vector<int> ascending = rising;
  const std::vector<int> descending(ascending.rbegin(), ascending.rend());
  const std::vector<int> constant(big, 7);
  for (const auto* input : {&ascending, &descending, &constant}) {
    CHECK(sorts_like_std(*input, [](auto& v) { ppqsort::sort(v.begin(), v.end()); }));
    if (run_parallel) {
      CHECK(sorts_like_std(*input, [](auto& v) { ppqsort::sort(ppqsort::execution::par, v.begin(), v.end(), 4); }));
    }
  }
}

void test_strings() {
  const std::vector<int> numbers = random_ints(300000);
  std::vector<std::string> words;
  words.reserve(numbers.size());
  for (int n : numbers) words.push_back(std::to_string(n % 100000));
  std::vector<std::string> expected = words;
  std::sort(expected.begin(), expected.end());
  // Strings take the branching partition unless a *_force_branchless policy asks otherwise.
  std::vector<std::string> sequential = words;
  ppqsort::sort(sequential.begin(), sequential.end());
  CHECK(sequential == expected);
  std::vector<std::string> branchless = words;
  ppqsort::sort(ppqsort::execution::seq_force_branchless, branchless.begin(), branchless.end());
  CHECK(branchless == expected);
  if (run_parallel) {
    std::vector<std::string> parallel = words;
    ppqsort::sort(ppqsort::execution::par, parallel.begin(), parallel.end(), 4);
    CHECK(parallel == expected);
    ppqsort::sort(ppqsort::execution::par_force_branchless, words.begin(), words.end(), 3);
    CHECK(words == expected);
  }
}

}  // namespace

int main() {
  test_small();
  test_policies();
  test_patterns();
  test_strings();
  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts(run_parallel ? "parallel checks ran" : "parallel checks skipped: _GLIBCXX_ASSERTIONS");
  std::puts("ppqsort 1.0.6 ok");
  return 0;
}
