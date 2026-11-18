#include <nonstd/observer_ptr.hpp>

#include <stdio.h>

namespace {

int failures = 0;

void check(bool ok, const char* what) {
  if (!ok) {
    fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

struct Point {
  int x;
  int y;
};

void TestObserve() {
  int a = 42;
  int b = 7;
  nonstd::observer_ptr<int> p = nonstd::make_observer(&a);
  check(*p == 42 && p.get() == &a && (p ? true : false), "make_observer");
  nonstd::observer_ptr<int> q;
  check(!q && q.get() == 0, "default is empty");
  *p = 43;
  check(a == 43, "writes through");

  p.reset(&b);
  check(*p == 7 && p.get() == &b, "reset");
  int* released = p.release();
  check(released == &b && !p && p == q, "release");

  p = nonstd::make_observer(&a);
  nonstd::swap(p, q);
  check(!p && q.get() == &a, "swap");
}

void TestCompare() {
  int pair[2] = {1, 2};
  nonstd::observer_ptr<int> first = nonstd::make_observer(&pair[0]);
  nonstd::observer_ptr<int> second = nonstd::make_observer(&pair[1]);
  check(first == nonstd::make_observer(&pair[0]) && first != second,
        "equality");
  check(first < second && !(second < first) && second > first &&
            first <= first && second >= first,
        "ordering");

  Point point = {1, 2};
  nonstd::observer_ptr<Point> observed(&point);
  observed->y = 5;
  check(point.y == 5 && (*observed).x == 1, "member access");
}

}  // namespace

int main() {
  TestObserve();
  TestCompare();
  if (failures != 0) {
    fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  puts("observer-ptr-lite consumer test passed");
  return 0;
}
