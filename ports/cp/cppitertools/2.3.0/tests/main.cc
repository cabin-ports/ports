// Runs itertools over small inputs and compares each result with the
// sequence written out by hand.
#include <cppitertools/itertools.hpp>
#include <cppitertools/zip_longest.hpp>

#include <cstdio>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace {

bool ok = true;

void check(bool pass, const char* what) {
  if (!pass) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ok = false;
  }
}

using ints = std::vector<int>;

template <typename Range>
ints collect(Range&& r) {
  ints out;
  for (auto&& x : r) out.push_back(x);
  return out;
}

template <typename Range>
std::vector<ints> collect_groups(Range&& r) {
  std::vector<ints> out;
  for (auto&& g : r) out.push_back(collect(g));
  return out;
}

}  // namespace

int main() {
  const ints v{1, 2, 3, 4, 5};

  check(collect(iter::range(0, 10, 3)) == ints({0, 3, 6, 9}), "range step");
  check(collect(iter::range(3, 0, -1)) == ints({3, 2, 1}), "range down");

  {
    const ints values{10, 20, 30};
    ints idx, val;
    for (auto&& [i, e] : iter::enumerate(values)) {
      idx.push_back(static_cast<int>(i));
      val.push_back(e);
    }
    check(idx == ints({0, 1, 2}) && val == values, "enumerate");
  }

  {
    const std::vector<std::string> names{"a", "b", "c"};
    ints nums;
    std::string joined;
    for (auto&& [n, s] : iter::zip(v, names)) {
      nums.push_back(n);
      joined += s;
    }
    check(nums == ints({1, 2, 3}) && joined == "abc", "zip stops at shortest");
  }

  {
    const ints a{1, 2, 3};
    const ints b{10};
    ints left, right;
    std::vector<bool> has_left, has_right;
    for (auto&& [x, y] : iter::zip_longest(a, b)) {
      has_left.push_back(static_cast<bool>(x));
      has_right.push_back(static_cast<bool>(y));
      left.push_back(x ? *x : -1);
      right.push_back(y ? *y : -1);
    }
    check(left == ints({1, 2, 3}) && right == ints({10, -1, -1}) &&
              has_left == std::vector<bool>({true, true, true}) &&
              has_right == std::vector<bool>({true, false, false}),
          "zip_longest pads with empty optionals");
  }

  check(collect(iter::chain(ints{1, 2}, ints{3}, ints{4, 5})) == v, "chain");
  check(collect(iter::filter([](int x) { return x % 2 == 0; }, v)) ==
            ints({2, 4}),
        "filter");
  check(collect(iter::imap([](int x) { return x * x; }, v)) ==
            ints({1, 4, 9, 16, 25}),
        "imap");

  {
    ints unsorted{3, 1, 2};
    check(collect(iter::sorted(unsorted)) == ints({1, 2, 3}), "sorted");
    check(unsorted == ints({3, 1, 2}), "sorted leaves its input alone");
  }

  {
    const ints in{1, 1, 2, 3, 3, 3};
    ints keys;
    std::vector<ints> groups;
    for (auto&& [k, g] : iter::groupby(in)) {
      keys.push_back(k);
      groups.push_back(collect(g));
    }
    check(keys == ints({1, 2, 3}) &&
              groups == std::vector<ints>({{1, 1}, {2}, {3, 3, 3}}),
          "groupby");
  }

  {
    std::vector<std::pair<int, int>> got;
    for (auto&& [a, b] : iter::product(ints{1, 2}, ints{3, 4}))
      got.emplace_back(a, b);
    check(got == std::vector<std::pair<int, int>>(
                     {{1, 3}, {1, 4}, {2, 3}, {2, 4}}),
          "product");
  }

  check(collect_groups(iter::combinations(ints{1, 2, 3, 4}, 2)) ==
            std::vector<ints>(
                {{1, 2}, {1, 3}, {1, 4}, {2, 3}, {2, 4}, {3, 4}}),
        "combinations");
  check(collect_groups(iter::permutations(ints{1, 2, 3})) ==
            std::vector<ints>({{1, 2, 3},
                               {1, 3, 2},
                               {2, 1, 3},
                               {2, 3, 1},
                               {3, 1, 2},
                               {3, 2, 1}}),
        "permutations");

  check(collect(iter::slice(v, 1, 4)) == ints({2, 3, 4}), "slice");
  check(collect(iter::repeat(7, 3)) == ints({7, 7, 7}), "repeat");

  return ok ? 0 : 1;
}
