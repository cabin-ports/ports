#include <cstdio>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

#include "fpgen.hpp"

namespace {

int failures = 0;

#define CHECK(cond)                                                      \
  do {                                                                   \
    if (!(cond)) {                                                       \
      std::printf("%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); \
      ++failures;                                                        \
    }                                                                    \
  } while (0)

fpgen::generator<long> fibonacci(int n) {
  long a = 0, b = 1;
  for (int i = 0; i < n; i++) {
    co_yield a;
    long next = a + b;
    a = b;
    b = next;
  }
}

} // namespace

int main() {
  // User-written coroutine, consumed with range-for.
  std::vector<long> fib;
  for (long v : fibonacci(10))
    fib.push_back(v);
  CHECK((fib == std::vector<long>{0, 1, 1, 2, 3, 5, 8, 13, 21, 34}));

  std::vector<int> v{1, 2, 3, 4};
  CHECK(fpgen::sum(fpgen::from(v)) == 10);
  CHECK(fpgen::count(fpgen::from(v)) == 4);

  // Sources.
  CHECK(fpgen::sum(fpgen::take(fpgen::inc(1), 5)) == 15);
  std::string digits;
  size_t index_sum = 0;
  for (auto [i, x] : fpgen::enumerate(v)) {
    index_sum += i;
    digits += std::to_string(x);
  }
  CHECK(index_sum == 6 && digits == "1234");
  std::istringstream lines("alpha\nbeta\ngamma");
  std::string joined;
  for (const auto &l : fpgen::from_lines(lines))
    joined += l + "|";
  CHECK(joined == "alpha|beta|gamma|");

  // Manipulators.
  CHECK(fpgen::sum(fpgen::map(fpgen::from(v), [](int x) { return x * x; })) == 30);
  std::vector<int> evens;
  for (int x : fpgen::filter(fpgen::from(v), [](int x) { return x % 2 == 0; }))
    evens.push_back(x);
  CHECK((evens == std::vector<int>{2, 4}));
  CHECK(fpgen::sum(fpgen::drop(fpgen::from(v), 2)) == 7);
  CHECK(fpgen::sum(fpgen::take_while(fpgen::inc(1), [](int x) { return x < 5; })) == 10);
  CHECK(fpgen::sum(fpgen::drop_while(fpgen::from(v), [](int x) { return x < 3; })) == 7);
  std::string pairs;
  auto letters = fpgen::map(fpgen::inc(0), [](int i) { return static_cast<char>('a' + i); });
  for (auto [n, c] : fpgen::zip(fpgen::from(v), letters))
    pairs += std::to_string(n) + c;
  CHECK(pairs == "1a2b3c4d");

  // Aggregators.
  CHECK(fpgen::fold<long>(fpgen::from(v), [](long acc, int x) { return acc * 10 + x; }) == 1234);
  CHECK(fpgen::fold<long>(fpgen::from(v), [](long acc, int x) { return acc + x; }, 7L) == 17);

  if (failures == 0)
    std::puts("fpgen OK");
  return failures == 0 ? 0 : 1;
}
