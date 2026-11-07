// C++98: the probe compiles at the interface minimum, so only the C++98
// API is used here.  The same calls hold for std::span, which nonstd::span
// names from C++20.
#include <nonstd/span.hpp>

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char* what, int line) {
  if (!ok) {
    std::fprintf(stderr, "main.cc:%d: check failed: %s\n", line, what);
    ++failures;
  }
}
#define CHECK(cond) check((cond), #cond, __LINE__)

int sum(nonstd::span<const int> s) {
  int total = 0;
  for (nonstd::span<const int>::iterator it = s.begin(); it != s.end(); ++it)
    total += *it;
  return total;
}

}  // namespace

int main() {
  CHECK(std::strcmp(span_lite_VERSION, "0.11.0") == 0);

  int values[] = {1, 2, 3, 4, 5};
  nonstd::span<int> all(values);
  CHECK(all.size() == 5);
  CHECK(all.size_bytes() == 5 * sizeof(int));
  CHECK(!all.empty());
  CHECK(all.data() == values);
  CHECK(all.front() == 1 && all.back() == 5 && all[2] == 3);
  CHECK(sum(all) == 15);  // span<int> converts to span<const int>

  nonstd::span<int> counted(values + 1, 3);
  nonstd::span<int> ranged(values + 1, values + 4);
  CHECK(counted.data() == values + 1 && counted.size() == 3);
  CHECK(ranged.data() == values + 1 && ranged.size() == 3);
  nonstd::span<int> none;
  CHECK(none.empty() && none.size() == 0 && none.data() == NULL);

  // Dynamic subviews alias the array.
  nonstd::span<int> head = all.first(2);
  nonstd::span<int> tail = all.last(2);
  nonstd::span<int> middle = all.subspan(1, 3);
  nonstd::span<int> rest = all.subspan(3);
  CHECK(head.data() == values && head.size() == 2);
  CHECK(tail.data() == values + 3 && tail.size() == 2);
  CHECK(middle.data() == values + 1 && middle.size() == 3);
  CHECK(rest.data() == values + 3 && rest.size() == 2);
  CHECK(all.subspan(5).empty());

  // Static extents, and subviews whose extent is a template argument.
  nonstd::span<int, 5> fixed(values);
  CHECK(static_cast<std::size_t>(nonstd::span<int, 5>::extent) == 5);
  CHECK(static_cast<std::size_t>(nonstd::span<int>::extent) ==
        nonstd::dynamic_extent);
  nonstd::span<int, 2> fixed_head = fixed.first<2>();
  nonstd::span<int, 2> fixed_tail = fixed.last<2>();
  nonstd::span<int, 3> fixed_middle = fixed.subspan<1, 3>();
  CHECK(fixed_head.data() == values && fixed_head.size() == 2);
  CHECK(fixed_tail.data() == values + 3 && fixed_tail.size() == 2);
  CHECK(fixed_middle.data() == values + 1 && fixed_middle.size() == 3);

  // Writes through a span reach the array; reverse iteration walks it back.
  middle[0] = 20;
  for (nonstd::span<int>::iterator it = tail.begin(); it != tail.end(); ++it)
    *it *= 10;
  const int expected[] = {50, 40, 3, 20, 1};
  CHECK(std::vector<int>(all.rbegin(), all.rend()) ==
        std::vector<int>(expected, expected + 5));

  // A view of a vector's storage, and span copy assignment.
  std::vector<double> samples(4, 0.5);
  nonstd::span<double> view(&samples[0], samples.size());
  view[3] = 2.0;
  CHECK(samples[3] == 2.0 && view.size_bytes() == 4 * sizeof(double));
  nonstd::span<int> copy = all;
  copy = head;
  CHECK(copy.data() == values && copy.size() == 2);

  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("span-lite consumer test passed");
  return 0;
}
