#include <Leopard/ThreadPool.h>

#include <algorithm>
#include <cstdio>
#include <functional>
#include <future>
#include <numeric>
#include <random>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char *what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

long sum_range(std::vector<long>::iterator b, std::vector<long>::iterator e) {
  return std::accumulate(b, e, 0L);
}

} // namespace

int main() {
  hmthrp::ThreadPool pool(2);
  check(pool.capacity_threads() == 2, "capacity");

  check(pool.dispatch(false, [](int a, int b) { return a + b; }, 2, 3).get() ==
            5,
        "dispatch returns the task value");

  std::vector<std::future<long>> futs;
  for (long i = 1; i <= 100; ++i)
    futs.push_back(pool.dispatch(false, [](long x) { return x * x; }, i));
  long squares = 0;
  for (auto &f : futs)
    squares += f.get();
  check(squares == 338350, "sum of 100 task results");

  std::vector<long> v(1000);
  std::iota(v.begin(), v.end(), 1L);
  auto parts = pool.parallel_loop(v.begin(), v.end(), sum_range);
  long total = 0;
  for (auto &f : parts)
    total += f.get();
  check(total == 500500, "parallel_loop block sums");

  // A threshold of 64 makes the 1000 elements recurse through the pool.
  std::vector<int> data(1000);
  for (int i = 0; i < 1000; ++i)
    data[i] = static_cast<int>((i * 7919L) % 1009);
  std::vector<int> expected(data);
  std::sort(expected.begin(), expected.end());
  pool.parallel_sort<std::vector<int>::iterator, 64>(data.begin(), data.end());
  check(data == expected, "parallel_sort ascending");
  check(data.front() == 0 && data.back() == 1008, "sorted ends");

  // Sorted input recurses once per element (first-element pivot).
  std::shuffle(data.begin(), data.end(), std::mt19937(1));
  pool.parallel_sort<std::vector<int>::iterator, std::greater<int>, 64>(
      data.begin(), data.end(), std::greater<int>());
  std::reverse(expected.begin(), expected.end());
  check(data == expected, "parallel_sort with comparator");

  check(pool.shutdown(), "shutdown");
  check(pool.is_shutdown(), "is_shutdown");
  return failures == 0 ? 0 : 1;
}
