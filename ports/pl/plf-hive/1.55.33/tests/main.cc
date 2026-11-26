#include <plf_hive.h>

#include <cstdio>
#include <functional>
#include <iterator>
#include <vector>

static int failures = 0;

#define CHECK(cond)                                                    \
  do {                                                                 \
    if (!(cond)) {                                                     \
      std::printf("%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); \
      ++failures;                                                      \
    }                                                                  \
  } while (0)

static int sum(const plf::hive<int> &h) {
  int total = 0;
  for (const int v : h) total += v;
  return total;
}

int main() {
  plf::hive<int> h;
  std::vector<plf::hive<int>::iterator> its;
  for (int i = 1; i <= 10; ++i) its.push_back(h.insert(i));
  CHECK(h.size() == 10);
  CHECK(sum(h) == 55);

  h.erase(its[4]);  // 5
  h.erase(its[5]);  // 6
  CHECK(h.size() == 8);
  CHECK(sum(h) == 44);

  h.insert(100);
  CHECK(h.size() == 9);
  CHECK(sum(h) == 144);
  CHECK(std::distance(h.begin(), h.end()) == 9);

  h.sort();
  std::vector<int> sorted(h.begin(), h.end());
  CHECK((sorted == std::vector<int>{1, 2, 3, 4, 7, 8, 9, 10, 100}));
  h.sort(std::greater<int>());
  CHECK(*h.begin() == 100);
  CHECK(*--plf::hive<int>::iterator(h.end()) == 1);
  CHECK(sum(h) == 144);

  // get_iterator maps an element pointer back to its iterator.
  auto it = h.begin();
  for (int i = 0; i < 3; ++i) ++it;
  CHECK(h.get_iterator(&*it) == it);
  CHECK(*h.get_iterator(&*it) == 8);

  plf::hive<int> r;
  r.reserve(1000);
  CHECK(r.size() == 0);
  CHECK(r.capacity() >= 1000);
  const auto cap = r.capacity();
  r.reserve(10);  // already large enough: no change
  CHECK(r.capacity() == cap);
  r.insert(7);
  CHECK(r.capacity() == cap);
  CHECK(r.size() == 1);

  const plf::hive<int> &ch = h;
  int count = 0, total = 0;
  for (const int v : ch) {
    ++count;
    total += v;
  }
  CHECK(count == 9);
  CHECK(total == 144);

  return failures == 0 ? 0 : 1;
}
