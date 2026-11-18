#include <nonstd/ring.hpp>
#include <nonstd/ring_span.hpp>

#include <cstdio>
#include <numeric>

static int failures = 0;

static void check(bool ok, const char* what) {
  if (!ok) {
    std::printf("FAIL %s\n", what);
    ++failures;
  }
}

template <class Range>
static bool holds(const Range& range, typename Range::value_type a,
                  typename Range::value_type b, typename Range::value_type c) {
  typename Range::const_iterator it = range.begin();
  if (range.size() != 3 || *it != a || *++it != b || *++it != c) {
    return false;
  }
  return ++it == range.end();
}

int main() {
  // README.md:21-61 and its recorded output.
  double arr[] = {2.0, 3.0, 5.0};
  double coeff[] = {0.25, 0.5, 0.25};
  nonstd::ring_span<double> buffer(arr, arr + 3, arr, 3);
  check(holds(buffer, 2.0, 3.0, 5.0), "ring_span holds 2, 3, 5");
  check(buffer.full(), "ring_span is full");

  buffer.push_back(7.0);
  check(holds(buffer, 3.0, 5.0, 7.0), "push_back(7) gives 3, 5, 7");
  check(buffer.front() == 3.0 && buffer.back() == 7.0, "front 3, back 7");
  check(arr[0] == 7.0, "push_back overwrote the oldest slot");
  check(std::inner_product(buffer.begin(), buffer.end(), coeff, 0.0) == 5.0,
        "filter result 5");

  nonstd::ring<int[3]> owned;
  check(owned.empty() && owned.capacity() == 3, "owning ring starts empty");
  for (int i = 1; i <= 4; ++i) {
    owned.push_back(i);
  }
  check(owned.full() && holds(owned, 2, 3, 4), "owning ring keeps 2, 3, 4");
  check(owned.pop_front() == 2 && owned.size() == 2 && owned.front() == 3,
        "pop_front returns 2");

  if (failures != 0) {
    return 1;
  }
  std::printf("ring-span-lite 0.7.0 C++ ok\n");
  return 0;
}
