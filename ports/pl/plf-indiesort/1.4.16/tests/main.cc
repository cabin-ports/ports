#include <plf_indiesort.h>

#include <cstdio>
#include <functional>
#include <list>
#include <vector>

static int failures = 0;

#define CHECK(cond)                                                    \
  do {                                                                 \
    if (!(cond)) {                                                     \
      std::printf("%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); \
      ++failures;                                                      \
    }                                                                  \
  } while (0)

struct Big {
  int key;
  char pad[60];
};

struct Huge {
  int key;
  char pad[196];
};

static bool huge_by_key(const Huge &a, const Huge &b) { return a.key < b.key; }

static bool by_key(const Big &a, const Big &b) { return a.key < b.key; }

int main() {
  const int in[] = {5, 3, 9, 1, 7, 3};

  // Non-random-access container, default and custom comparator.
  std::list<int> l(in, in + 6);
  plf::indiesort(l);
  const int asc[] = {1, 3, 3, 5, 7, 9};
  CHECK(std::vector<int>(l.begin(), l.end()) == std::vector<int>(asc, asc + 6));
  plf::indiesort(l, std::greater<int>());
  const int desc[] = {9, 7, 5, 3, 3, 1};
  CHECK(std::vector<int>(l.begin(), l.end()) == std::vector<int>(desc, desc + 6));

  // Iterator range of a non-random-access container, and an empty container.
  std::list<int> f(in, in + 6);
  plf::indiesort(f.begin(), f.end());
  std::vector<int> fv(f.begin(), f.end());
  CHECK(fv == std::vector<int>(asc, asc + 6));
  std::list<int> empty;
  plf::indiesort(empty);
  CHECK(empty.empty());

  // Random-access container of large elements, against a hand-computed order.
  std::vector<Big> v(40);
  for (int i = 0; i < 40; ++i) v[i].key = (i * 17) % 40;
  plf::indiesort(v, by_key);
  bool sorted = true;
  for (int i = 0; i < 40; ++i) sorted = sorted && v[i].key == i;
  CHECK(sorted);

  // Elements above upstream's 152-byte threshold take the indirect sort.
  Huge h[30];
  for (int i = 0; i < 30; ++i) h[i].key = (i * 7) % 30;
  plf::indiesort(h, h + 30, huge_by_key);
  bool hsorted = true;
  for (int i = 0; i < 30; ++i) hsorted = hsorted && h[i].key == i;
  CHECK(hsorted);

  // Raw pointers.
  int arr[] = {4, 2, 8, 6};
  plf::indiesort(arr, arr + 4);
  CHECK(arr[0] == 2 && arr[1] == 4 && arr[2] == 6 && arr[3] == 8);

  if (failures == 0) std::printf("plf_indiesort: all checks passed\n");
  return failures == 0 ? 0 : 1;
}
