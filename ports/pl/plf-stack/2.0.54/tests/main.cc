// C++98-compatible: the probe builds this at the declared c++98.
#include <plf_stack.h>

#include <cstdio>

namespace {

int failures = 0;

void check(bool ok, const char *what) {
  if (!ok) {
    std::printf("FAIL: %s\n", what);
    ++failures;
  }
}

}  // namespace

int main() {
  plf::stack<int> s;
  check(s.empty(), "new stack is empty");
  for (int i = 1; i <= 1000; ++i) s.push(i);
  check(s.size() == 1000, "size after 1000 pushes");
  check(s.top() == 1000, "top after pushes");
  for (int i = 0; i < 10; ++i) s.pop();
  check(s.size() == 990 && s.top() == 990, "top after 10 pops");

  // Copies compare equal until one changes.
  plf::stack<int> copy(s);
  check(copy == s, "copy equals original");
  copy.pop();
  copy.push(1);
  check(copy.size() == s.size() && copy != s, "same-size copy with another top differs");

  // append() of a source whose destination top block is not full keeps order.
  plf::stack<int> a(3, 8), b(3, 3);
  for (int i = 1; i <= 4; ++i) a.push(i);
  for (int i = 10; i <= 15; ++i) b.push(i);
  a.append(b);
  check(b.empty(), "append empties the source");
  check(a.size() == 10, "size after append");
  const int expected[10] = {15, 14, 13, 12, 11, 10, 4, 3, 2, 1};
  for (int i = 0; i < 10 && !a.empty(); ++i) {
    check(a.top() == expected[i], "append pop order");
    a.pop();
  }

  s.clear();
  check(s.empty() && s.size() == 0, "clear empties the stack");
  s.push(7);
  check(s.size() == 1 && s.top() == 7, "push after clear");

  if (failures == 0) std::printf("plf-stack OK\n");
  return failures == 0 ? 0 : 1;
}
