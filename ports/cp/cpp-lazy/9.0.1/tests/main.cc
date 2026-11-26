// The range and format expectations are upstream's (range.cpp:40-44 for 3
// steps, algorithm.cpp:60-62 for lz::format with {fmt}); the map over filter
// is original, with piping as in filter.cpp:25.
#include <Lz/filter.hpp>
#include <Lz/map.hpp>
#include <Lz/procs/to.hpp>
#include <Lz/range.hpp>
#include <Lz/stream.hpp>

#include <array>
#include <cstdio>
#include <string>
#include <vector>

namespace {

bool ok = true;

void check(bool pass, const char* what) {
  if (!pass) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ok = false;
  }
}

}  // namespace

int main() {
  const std::vector<int> v{1, 2, 3, 4, 5, 6};

  auto squares = lz::map(lz::filter(v, [](int i) { return i % 2 == 0; }),
                         [](int i) { return i * i; }) |
                 lz::to<std::vector>();
  check(squares == std::vector<int>({4, 16, 36}), "map over filter");

  auto stepped = lz::range(0, 10, 3) | lz::to<std::vector>();
  check(stepped == std::vector<int>({0, 3, 6, 9}), "range with a step");

  const std::array<int, 5> arr = {1, 2, 3, 4, 5};
  check(lz::format(arr, ", ", "{:02}") == "01, 02, 03, 04, 05", "format spec");
  check(lz::format(arr) == "1, 2, 3, 4, 5", "format default");

  return ok ? 0 : 1;
}
