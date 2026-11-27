// C++98-compatible: the probe builds this at the declared c++98.
#include <plf_colony.h>

#include <algorithm>
#include <cstdio>
#include <iterator>
#include <vector>

static int failures = 0;

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::printf("%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond);   \
      ++failures;                                                            \
    }                                                                        \
  } while (0)

typedef plf::colony<int> colony_t;

static int sum(const colony_t &c) {
  int total = 0;
  for (colony_t::const_iterator it = c.begin(); it != c.end(); ++it)
    total += *it;
  return total;
}

int main() {
  colony_t c;
  for (int i = 0; i < 10; ++i) c.insert(i);
  CHECK(c.size() == 10);
  CHECK(sum(c) == 45);

  c.erase(std::find(c.begin(), c.end(), 3));
  CHECK(c.size() == 9);

  // Erase the range [4, 7); the erased block joins the skipblock left by 3.
  colony_t::iterator first = std::find(c.begin(), c.end(), 4);
  colony_t::const_iterator saved = first;
  int *p4 = &*first;
  colony_t::iterator last = std::find(c.begin(), c.end(), 7);
  c.erase(first, last);
  CHECK(c.size() == 6);
  CHECK(sum(c) == 27);
  CHECK(!c.is_active(saved));
  CHECK(c.get_iterator(p4) == c.end());

  // A new element reuses an erased slot.
  c.insert(100);
  CHECK(c.size() == 7);
  CHECK(sum(c) == 127);

  c.sort();
  const int expected[] = {0, 1, 2, 7, 8, 9, 100};
  std::vector<int> sorted(c.begin(), c.end());
  CHECK(sorted.size() == 7);
  CHECK(std::equal(sorted.begin(), sorted.end(), expected));

  colony_t::iterator it = c.begin();
  std::advance(it, 3);
  CHECK(*it == 7);
  CHECK(c.get_iterator(&*it) == it);
  CHECK(c.is_active(it));
  CHECK(std::distance(c.begin(), c.end()) == 7);

  // A colony that never allocated: distance() between equal iterators is 0.
  colony_t e;
  CHECK(e.empty());
  CHECK(distance(e.begin(), e.end()) == 0);

  c.clear();
  CHECK(c.empty());
  CHECK(c.size() == 0);

  return failures == 0 ? 0 : 1;
}
