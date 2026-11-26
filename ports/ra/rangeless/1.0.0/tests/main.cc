#include <fn.hpp>

#include <cstdio>
#include <string>
#include <vector>

namespace fn = rangeless::fn;
using fn::operators::operator%;

namespace {

int failures = 0;

void check(bool ok, const char *what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

using ints = std::vector<int>;

ints make(std::initializer_list<int> l) { return ints(l); }

} // namespace

int main() {
  // where + lazy transform, materialized by to_vector
  check((make({5, 3, 8, 1, 4, 3}) % fn::where([](int x) { return x % 2 == 1; }) %
         fn::transform([](int x) { return x * 10; }) % fn::to_vector()) ==
            make({50, 30, 10, 30}),
        "where/transform/to_vector");

  // stable sort and sort_by with a descending key
  check((make({5, 3, 8, 1, 4, 3}) % fn::sort()) == make({1, 3, 3, 4, 5, 8}),
        "sort");
  check((make({5, 3, 8, 1, 4, 3}) % fn::sort_by([](int x) { return -x; })) ==
            make({8, 5, 4, 3, 3, 1}),
        "sort_by");

  // unique_all sorts by key and keeps one of each
  check((make({3, 1, 3, 2, 1}) % fn::unique_all()) == make({1, 2, 3}),
        "unique_all");

  // group_adjacent_by splits runs of equal keys; concat flattens them back
  std::vector<ints> groups = make({1, 2, 11, 12, 13, 25, 3}) %
                             fn::group_adjacent_by([](int x) { return x / 10; });
  check(groups.size() == 4, "group count");
  if (groups.size() == 4) {
    check(groups[0] == make({1, 2}), "group 0");
    check(groups[1] == make({11, 12, 13}), "group 1");
    check(groups[2] == make({25}), "group 2");
    check(groups[3] == make({3}), "group 3");
  }
  check((std::move(groups) % fn::concat()) == make({1, 2, 11, 12, 13, 25, 3}),
        "concat");

  // take_first / drop_first
  check((make({4, 5, 6, 7, 8}) % fn::take_first(2) % fn::to_vector()) ==
            make({4, 5}),
        "take_first");
  check((make({4, 5, 6, 7, 8}) % fn::drop_first(2) % fn::to_vector()) ==
            make({6, 7, 8}),
        "drop_first");

  // foldl with a non-trivial accumulator type
  check((make({1, 2, 3}) %
         fn::foldl(std::string("^"), [](std::string out, int in) {
           return std::move(out) + "|" + std::to_string(in);
         })) == "^|1|2|3",
        "foldl");

  // an infinite lazy seq must be cut by take_first
  int n = 0;
  check((fn::seq([&] { return n++; }) % fn::where([](int x) { return x % 3 == 0; }) %
         fn::transform([](int x) { return x * x; }) % fn::take_first(4) %
         fn::to_vector()) == make({0, 9, 36, 81}),
        "seq generator");

  if (failures != 0) {
    return 1;
  }
  std::puts("ok");
  return 0;
}
