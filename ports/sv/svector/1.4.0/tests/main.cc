#include <ankerl/svector.h>

#include <cstdio>
#include <string>
#include <utility>

static_assert(ANKERL_SVECTOR_VERSION_MAJOR == 1 && ANKERL_SVECTOR_VERSION_MINOR == 4 &&
              ANKERL_SVECTOR_VERSION_PATCH == 0);

namespace {

int failures = 0;

void check(bool ok, const char *what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    failures++;
  }
}

} // namespace

int main() {
  // push_back far past the inline capacity.
  ankerl::svector<int, 7> v;
  for (int i = 0; i < 100; ++i) {
    v.push_back(i);
  }
  check(v.size() == 100, "size after push_back");
  check(v.front() == 0 && v.back() == 99 && v[50] == 50, "elements after growth");

  // insert and erase.
  ankerl::svector<int, 3> w{1, 2, 3};
  w.insert(w.begin() + 1, 2, 9);
  check((w == ankerl::svector<int, 3>{1, 9, 9, 2, 3}), "insert count copies");
  w.erase(w.begin() + 1, w.begin() + 3);
  check((w == ankerl::svector<int, 3>{1, 2, 3}), "erase range");
  check(std::erase_if(w, [](int x) { return x == 2; }) == 1, "erase_if count");
  check((w == ankerl::svector<int, 3>{1, 3}), "erase_if result");

  // A non-trivial element type that outgrows the inline storage.
  ankerl::svector<std::string, 2> s;
  s.emplace_back("a");
  s.emplace_back("b");
  s.emplace_back("c");
  check(s.size() == 3 && s[0] == "a" && s[2] == "c", "string elements past inline capacity");

  // Copy and move.
  ankerl::svector<int, 7> copy = v;
  check(copy == v, "copy equals original");
  copy.push_back(1);
  check(copy != v, "copy diverges after push_back");
  ankerl::svector<int, 7> moved = std::move(copy);
  check(moved.size() == 101 && moved.back() == 1, "move keeps elements");

  return failures == 0 ? 0 : 1;
}
