#include <veque.hpp>

#include <cstdio>
#include <functional>
#include <string>

namespace {

int failures = 0;

void check(bool ok, const char *what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    failures++;
  }
}

template <typename T> bool is(const veque::veque<T> &v, std::initializer_list<T> want) {
  if (v.size() != want.size()) return false;
  size_t i = 0;
  for (const T &w : want)
    if (v[i++] != w) return false;
  return true;
}

} // namespace

int main() {
  // Both ends.
  veque::veque<int> v;
  v.push_back(2);
  v.push_back(3);
  v.push_front(1);
  v.push_front(0);
  check(v.size() == 4, "size after pushes");
  check(is(v, {0, 1, 2, 3}), "push_front/push_back order");
  check(v.front() == 0 && v.back() == 3, "front/back");
  check(v[1] == 1 && v[2] == 2, "operator[]");
  check(v.pop_front_element() == 0, "pop_front_element returns the front element");
  v.pop_back();
  check(is(v, {1, 2}), "after pop_front/pop_back");

  // API.md: resize_front "adds or removes from the front".
  v.resize_front(4, 9);
  check(is(v, {9, 9, 1, 2}), "resize_front grows at the front");
  v.resize_front(3);
  check(is(v, {9, 1, 2}), "resize_front shrinks from the front");
  v.resize_back(4, 7);
  check(is(v, {9, 1, 2, 7}), "resize_back grows at the back");

  // Insert in the middle.
  auto it = v.insert(v.begin() + 2, 5);
  check(*it == 5 && it == v.begin() + 2, "insert returns iterator to new element");
  check(is(v, {9, 1, 5, 2, 7}), "insert in the middle");

  // Equality and std::hash (specialized in veque.hpp).
  veque::veque<int> a{1, 2, 3};
  veque::veque<int> b;
  b.push_back(3);
  b.push_front(2);
  b.push_front(1);
  check(a == b, "equal veques compare equal");
  check(std::hash<veque::veque<int>>()(a) == std::hash<veque::veque<int>>()(b), "equal veques hash equal");
  b.push_back(4);
  check(a != b, "different veques compare unequal");
  check(std::hash<veque::veque<int>>()(a) != std::hash<veque::veque<int>>()(b), "different veques hash differently");

  // Non-trivial element type.
  veque::veque<std::string> s;
  s.push_front("b");
  s.push_front("a");
  s.push_back("c");
  check(s.size() == 3 && s[0] == "a" && s[2] == "c", "string elements");

  return failures ? 1 : 0;
}
