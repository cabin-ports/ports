// Exercises tl-ranges' views and helpers with exact expected sequences:
// enumerate, chunk, chunk_by, adjacent_transform, zip, cartesian_product,
// concat, k_combinations (every k-tuple of elements, with repetition, in
// lexicographic order, as upstream's test expects), repeat_n, tl::to, fold_left
// and the predicates.
// It does not subtract two repeat_n iterators or measure a k_combinations view
// other than through its size() (cabin.toml).

#include <tl/adjacent_transform.hpp>
#include <tl/cartesian_product.hpp>
#include <tl/chunk.hpp>
#include <tl/chunk_by.hpp>
#include <tl/enumerate.hpp>
#include <tl/fold.hpp>
#include <tl/functional/predicates.hpp>
#include <tl/k_combinations.hpp>
#include <tl/repeat_n.hpp>
#include <tl/to.hpp>
#include <tl/zip.hpp>
#if !defined(__clang__)
// Clang rejects tl/concat.hpp (cabin.toml).
#include <tl/concat.hpp>
#endif

#include <cstdio>
#include <functional>
#include <list>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

static int failures = 0;

static void check(bool ok, const char *what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

static void test_enumerate() {
  std::vector<char> letters{'a', 'b', 'c'};
  std::string seen;
  for (auto &&[index, letter] : letters | tl::views::enumerate) {
    seen += std::to_string(index);
    seen += letter;
    letter = static_cast<char>(letter - 'a' + 'A');
  }
  check(seen == "0a1b2c", "enumerate pairs each element with its index");
  check(letters == std::vector<char>{'A', 'B', 'C'}, "enumerate refers to the elements");
}

static void test_chunk_and_to() {
  std::vector<int> v{0, 1, 2, 3, 4, 5, 6};
  std::vector<std::vector<int>> groups;
  for (auto &&group : v | tl::views::chunk(3)) {
    groups.push_back(tl::to<std::vector<int>>(group));
  }
  check(groups == std::vector<std::vector<int>>{{0, 1, 2}, {3, 4, 5}, {6}}, "chunk(3)");
}

static void test_chunk_by_and_predicates() {
  std::vector<int> v{1, 1, 2, 3, 3, 3};
  std::vector<std::vector<int>> runs;
  for (auto &&run : v | tl::views::chunk_by(std::equal_to<>{})) {
    runs.push_back(tl::to<std::vector<int>>(run));
  }
  check(runs == std::vector<std::vector<int>>{{1, 1}, {2}, {3, 3, 3}}, "chunk_by(equal_to)");

  auto two = tl::both(tl::gt(1), tl::lt(3));
  int threes = 0, twos = 0;
  for (int x : v) {
    threes += tl::eq(3)(x) ? 1 : 0;
    twos += two(x) ? 1 : 0;
  }
  check(threes == 3 && twos == 1, "tl::eq and tl::both");
}

static void test_adjacent_transform() {
  std::vector<int> v{1, 2, 4, 8};
  std::vector<int> sums;
  for (int sum : v | tl::views::adjacent_transform<2>(std::plus<>{})) {
    sums.push_back(sum);
  }
  check(sums == std::vector<int>{3, 6, 12}, "adjacent_transform<2>(plus)");
}

static void test_zip() {
  std::vector<int> numbers{1, 2, 3};
  std::list<std::string> names{"one", "two"};
  std::vector<std::pair<int, std::string>> zipped;
  for (auto &&[n, name] : tl::views::zip(numbers, names)) {
    zipped.emplace_back(n, name);
    name += "!";
  }
  check(zipped == std::vector<std::pair<int, std::string>>{{1, "one"}, {2, "two"}}, "zip stops at the shorter range");
  check(names == std::list<std::string>{"one!", "two!"}, "zip refers to the elements");
}

static void test_cartesian_product() {
  std::vector<int> a{1, 2};
  std::vector<char> b{'x', 'y', 'z'};
  std::string seen;
  for (auto &&[n, c] : tl::views::cartesian_product(a, b)) {
    seen += std::to_string(n);
    seen += c;
  }
  check(seen == "1x1y1z2x2y2z", "cartesian_product order");
  check(std::ranges::distance(tl::views::cartesian_product(a, b)) == 6, "cartesian_product size");
}

static void test_concat() {
#if !defined(__clang__)
  std::vector<int> a{0, 1, 2};
  std::vector<int> empty;
  std::list<int> b{3, 4};
  std::vector<int> c{5};
  auto all = tl::views::concat(a, empty, b, c);
  std::vector<int> seen;
  for (int x : all) {
    seen.push_back(x);
  }
  check(seen == std::vector<int>{0, 1, 2, 3, 4, 5}, "concat, skipping an empty range");
  check(all.size() == 6, "concat size");
#endif
}

static void test_k_combinations() {
  std::vector<int> v{0, 1, 2};
  auto pairs = tl::views::k_combinations(v, 2);
  std::vector<std::vector<int>> seen;
  for (auto &&combination : pairs) {
    seen.push_back(tl::to<std::vector<int>>(combination));
  }
  check(seen == std::vector<std::vector<int>>{{0, 0}, {0, 1}, {0, 2}, {1, 0}, {1, 1},
                                              {1, 2}, {2, 0}, {2, 1}, {2, 2}},
        "k_combinations(v, 2)");
  check(pairs.size() == 9, "k_combinations size");
}

static void test_repeat_n() {
  std::vector<std::string> seen;
  for (auto &&s : tl::views::repeat_n(std::string("ab"), 3)) {
    seen.push_back(s);
  }
  check(seen == std::vector<std::string>{"ab", "ab", "ab"}, "repeat_n");
  check(std::ranges::distance(tl::views::repeat_n(7, 4)) == 4, "repeat_n size");
  check(std::ranges::empty(tl::views::repeat_n(7, 0)), "repeat_n of zero");
}

static void test_fold() {
  std::vector<int> v{10, 20, 30};
  check(tl::fold_left(v, 0, std::plus<>{}) == 60, "fold_left");
  check(tl::fold_left(v, 100, std::minus<>{}) == 40, "fold_left is left-associative");
}

int main() {
  test_enumerate();
  test_chunk_and_to();
  test_chunk_by_and_predicates();
  test_adjacent_transform();
  test_zip();
  test_cartesian_product();
  test_concat();
  test_k_combinations();
  test_repeat_n();
  test_fold();
  if (failures != 0) {
    return 1;
  }
  std::puts("tl-ranges 0.1.0 C++ ok");
  return 0;
}
