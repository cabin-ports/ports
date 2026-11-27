#include <plf_list.h>

#include <cstdio>
#include <functional>
#include <vector>

static int failures = 0;

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::printf("%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond);   \
      ++failures;                                                            \
    }                                                                        \
  } while (0)

typedef plf::list<int> list_t;

static int sum(const list_t &l) {
  int total = 0;
  for (list_t::const_iterator i = l.begin(); i != l.end(); ++i) total += *i;
  return total;
}

static std::vector<int> contents(const list_t &l) {
  return std::vector<int>(l.begin(), l.end());
}

static bool equals(const list_t &l, const int *expected, std::size_t n) {
  return contents(l) == std::vector<int>(expected, expected + n);
}

int main() {
  list_t l;
  for (int i = 1; i <= 5; ++i) l.push_back(i);
  l.push_front(0);
  CHECK(l.size() == 6);
  CHECK(l.front() == 0 && l.back() == 5);
  CHECK(sum(l) == 15);

  // Insertion and erasure keep the order and the other iterators valid.
  list_t::iterator three = l.begin();
  for (int i = 0; i < 3; ++i) ++three;
  CHECK(*three == 3);
  list_t::iterator ins = l.insert(three, 99);
  CHECK(*ins == 99 && *++list_t::iterator(ins) == 3);
  CHECK(*three == 3);
  list_t::iterator after = l.erase(ins);
  CHECK(after == three);
  CHECK(l.size() == 6);
  l.erase(three);
  const int e1[] = {0, 1, 2, 4, 5};
  CHECK(equals(l, e1, 5));
  l.pop_front();
  l.pop_back();
  const int e2[] = {1, 2, 4};
  CHECK(equals(l, e2, 3));

  // Reverse traversal.
  std::vector<int> back;
  for (list_t::reverse_iterator r = l.rbegin(); r != l.rend(); ++r)
    back.push_back(*r);
  CHECK(back.size() == 3 && back[0] == 4 && back[2] == 1);

  // Sort, unique, remove, reverse.
  list_t s;
  const int in[] = {5, 3, 3, 9, 1, 5, 5, 7};
  for (int i = 0; i < 8; ++i) s.push_back(in[i]);
  s.sort();
  const int sorted[] = {1, 3, 3, 5, 5, 5, 7, 9};
  CHECK(equals(s, sorted, 8));
  CHECK(s.unique() == 3);
  const int uniq[] = {1, 3, 5, 7, 9};
  CHECK(equals(s, uniq, 5));
  s.sort(std::greater<int>());
  CHECK(s.front() == 9 && s.back() == 1);
  s.reverse();
  CHECK(equals(s, uniq, 5));
  CHECK(s.remove(5) == 1);
  CHECK(s.size() == 4 && sum(s) == 20);

  // Merge of two sorted lists, then splice of one list into another.
  list_t a, b;
  a.push_back(1);
  a.push_back(4);
  b.push_back(2);
  b.push_back(3);
  a.merge(b);
  CHECK(b.empty());
  const int merged[] = {1, 2, 3, 4};
  CHECK(equals(a, merged, 4));
  list_t c;
  c.push_back(10);
  c.push_back(11);
  list_t::iterator pos = a.begin();
  ++pos;
  a.splice(pos, c);
  CHECK(c.empty());
  const int spliced[] = {1, 10, 11, 2, 3, 4};
  CHECK(equals(a, spliced, 6));

  // Copy, swap and equality.
  list_t copy(a);
  CHECK(copy == a);
  copy.push_back(5);
  CHECK(copy != a);
  copy.swap(a);
  CHECK(a.size() == 7 && copy.size() == 6);

  // Enough elements to span several internal node blocks, with reserve().
  list_t big;
  big.reserve(5000);
  for (int i = 0; i < 20000; ++i) big.push_back(i);
  CHECK(big.size() == 20000);
  long long total = 0;
  for (list_t::iterator i = big.begin(); i != big.end(); ++i) total += *i;
  CHECK(total == 199990000LL);
  big.erase(big.begin(), big.end());
  CHECK(big.empty());

  if (failures) return 1;
  std::puts("ok");
  return 0;
}
