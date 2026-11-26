#include <plf_queue.h>

#include <compare>
#include <cstdio>
#include <iterator>

// Only operator<, so <=> is synthesised from it.
struct Less {
  int v;
  friend bool operator<(const Less &a, const Less &b) { return a.v < b.v; }
};

int main() {
  int failures = 0;

  plf::queue<int> a, b;
  for (int i = 0; i < 100; ++i) {
    a.push(i);
    b.push(i);
  }
  b.push(100);
  if (!((a <=> b) < 0)) ++failures;
  if (!((b <=> a) > 0)) ++failures;
  a.push(100);
  if (!((a <=> b) == 0)) ++failures;

  // Same length, first difference decides.
  plf::queue<int> p, r;
  p.push(1); p.push(2); p.push(3);
  r.push(1); r.push(3); r.push(2);
  if (!((p <=> r) < 0)) ++failures;
  if (!((r <=> p) > 0)) ++failures;

  plf::queue<Less> x, y;
  x.push(Less{1});
  y.push(Less{2});
  if (!((x <=> y) < 0)) ++failures;
  if (!((y <=> x) > 0)) ++failures;

  plf::queue<int> c;
  for (int i = 1; i <= 10; ++i) c.push(i);
  int expect = 10;
  for (auto it = std::make_reverse_iterator(c.end()); it != std::make_reverse_iterator(c.begin()); ++it) {
    if (*it != expect--) ++failures;
  }
  if (expect != 0) ++failures;

  if (failures != 0) {
    std::printf("%d checks failed\n", failures);
    return 1;
  }
  std::puts("ok");
  return 0;
}
