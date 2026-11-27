// C++98-compatible: the probe builds this at the declared c++98.
#include <plf_queue.h>

#include <cstdio>
#include <string>

static int failures = 0;

#define CHECK(cond)                                                       \
  do {                                                                    \
    if (!(cond)) {                                                        \
      std::printf("%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); \
      ++failures;                                                         \
    }                                                                     \
  } while (0)

int main() {
  plf::queue<int> q;
  CHECK(q.empty());
  for (int i = 1; i <= 1000; ++i) q.push(i);
  CHECK(q.size() == 1000);
  CHECK(q.front() == 1);
  CHECK(q.back() == 1000);

  for (int i = 0; i < 500; ++i) q.pop();
  CHECK(q.size() == 500);
  CHECK(q.front() == 501);
  CHECK(q.back() == 1000);

  q.push(7);
  CHECK(q.back() == 7);
  CHECK(q.size() == 501);

  // Iteration runs front to back.
  long sum = 0;
  int n = 0;
  for (plf::queue<int>::const_iterator it = q.cbegin(); it != q.cend(); ++it) {
    sum += *it;
    ++n;
  }
  CHECK(n == 501);
  CHECK(sum == (501L + 1000L) * 500L / 2 + 7);
  CHECK(*q.begin() == 501);
  CHECK(*--q.end() == 7);

  plf::queue<int> copy(q);
  CHECK(copy == q);
  copy.pop();
  CHECK(copy != q);
  CHECK(copy.front() == 502);
  CHECK(q.front() == 501);
  CHECK(q < copy);
  CHECK(!(copy < q));

  // Same size, different content.
  plf::queue<int> other(q);
  other.pop();
  other.push(999);
  CHECK(other.size() == q.size());
  CHECK(other != q);
  CHECK(!(other == q));

  // Small blocks force many groups and group reuse across push/pop.
  plf::queue<int> small(4, 8);
  int next_push = 0, next_pop = 0;
  for (int round = 0; round < 50; ++round) {
    for (int i = 0; i < 13; ++i) small.push(next_push++);
    for (int i = 0; i < 11; ++i) {
      CHECK(small.front() == next_pop);
      small.pop();
      ++next_pop;
    }
  }
  CHECK(small.size() == 100);
  CHECK(small.front() == next_pop);
  CHECK(small.back() == next_push - 1);
  CHECK(small.capacity() >= small.size());

  small.reshape(16, 32);
  CHECK(small.size() == 100);
  CHECK(small.front() == next_pop);
  CHECK(small.back() == next_push - 1);
  small.shrink_to_fit();
  CHECK(small.capacity() >= 100);

  plf::queue<int> r;
  r.reserve(1000);
  CHECK(r.capacity() >= 1000);
  CHECK(r.size() == 0);
  r.push(1);
  r.swap(q);
  CHECK(r.size() == 501);
  CHECK(r.front() == 501);
  CHECK(q.size() == 1);
  CHECK(q.front() == 1);

  q.clear();
  CHECK(q.empty());
  CHECK(q.size() == 0);

  // Non-trivial elements are constructed in place and destroyed on pop and clear.
  plf::queue<std::string> s;
  s.push(std::string("alpha"));
  s.push(std::string(100, 'x'));
  s.push("gamma");
  CHECK(s.front() == "alpha");
  s.pop();
  CHECK(s.front().size() == 100);
  CHECK(s.back() == "gamma");
  s.clear();
  CHECK(s.empty());

  if (failures != 0) return 1;
  std::puts("ok");
  return 0;
}
