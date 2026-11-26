#include <cor3ntin/rangesnext/__detail.hpp>
#include <cor3ntin/rangesnext/enumerate.hpp>
#include <cor3ntin/rangesnext/generator.hpp>
#include <cor3ntin/rangesnext/product.hpp>
#include <cor3ntin/rangesnext/to.hpp>

#include <cstdio>
#include <set>
#include <string>
#include <tuple>
#include <vector>

namespace rn = cor3ntin::rangesnext;

static int failures = 0;

static void check(bool ok, const char* what) {
  if (!ok) {
    std::printf("FAIL %s\n", what);
    ++failures;
  }
}

static rn::generator<int> fibonacci(int n) {
  int a = 0, b = 1;
  for (int i = 0; i < n; ++i) {
    co_yield a;
    int next = a + b;
    a = b;
    b = next;
  }
}

int main() {
  // enumerate
  const std::vector<std::string> words{"alpha", "beta", "gamma"};
  std::size_t n = 0;
  for (auto&& [index, value] : rn::enumerate(words)) {
    check(index == static_cast<std::ptrdiff_t>(n), "enumerate index");
    check(value == words[n], "enumerate value");
    ++n;
  }
  check(n == 3, "enumerate count");

  // product
  const std::vector<char> symbols{'+', '-'};
  const std::vector<int> numbers{1, 2, 3};
  const std::vector<std::tuple<char, int>> expected_product{
      {'+', 1}, {'+', 2}, {'+', 3}, {'-', 1}, {'-', 2}, {'-', 3}};
  std::vector<std::tuple<char, int>> got_product;
  for (auto&& [c, i] : rn::product(symbols, numbers)) {
    got_product.emplace_back(c, i);
  }
  check(got_product == expected_product, "product sequence");

  // ranges::to
  const std::vector<int> source{3, 1, 3, 2};
  const auto vec = source | rn::to<std::vector<int>>();
  check(vec == source, "to<std::vector> contents");
  const auto set = source | rn::to<std::set<int>>();
  check(set == std::set<int>({1, 2, 3}), "to<std::set> contents");

  // generator
  std::vector<int> fib;
  for (int v : fibonacci(6)) {
    fib.push_back(v);
  }
  check(fib == std::vector<int>({0, 1, 1, 2, 3, 5}), "generator fibonacci");

  if (failures == 0) {
    std::puts("ok");
  }
  return failures;
}
