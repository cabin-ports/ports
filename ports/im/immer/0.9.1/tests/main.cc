#include <immer/algorithm.hpp>
#include <immer/array.hpp>
#include <immer/atom.hpp>
#include <immer/box.hpp>
#include <immer/flex_vector.hpp>
#include <immer/map.hpp>
#include <immer/set.hpp>
#include <immer/vector.hpp>
#include <immer/vector_transient.hpp>

#include <cstdio>
#include <string>

namespace {

int failures = 0;

void check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

void TestVector() {
  const auto v0 = immer::vector<int>{};
  const auto v1 = v0.push_back(13);
  const auto v2 = v1.set(0, 42);
  check(v0.empty() && v1.size() == 1 && v1[0] == 13 && v2[0] == 42,
        "push_back and set leave the originals alone");

  // Enough elements to fill many leaves under a branch node.
  auto big = v0;
  for (int i = 0; i < 1000; ++i) big = big.push_back(i);
  check(big.size() == 1000 && big[999] == 999 && big[512] == 512 &&
            v0.empty(),
        "1000 push_backs");
  check(immer::accumulate(big, 0L) == 499500L, "accumulate");
  const auto doubled = big.update(10, [](int x) { return x * 2; });
  check(doubled[10] == 20 && big[10] == 10, "update");

  auto t = big.transient();
  t.push_back(1000);
  t.set(0, -1);
  const auto v3 = t.persistent();
  check(v3.size() == 1001 && v3[0] == -1 && v3[1000] == 1000 &&
            big.size() == 1000 && big[0] == 0,
        "transient edits");
}

void TestFlexVector() {
  const auto a = immer::flex_vector<int>{1, 2};
  const auto b = immer::flex_vector<int>{3};
  const auto ab = a + b;
  check(ab.size() == 3 && ab[2] == 3 && a.size() == 2, "concatenation");
  const auto c = ab.insert(1, 9).drop(1).take(2);
  check(c == immer::flex_vector<int>{9, 2}, "insert, drop, take");
}

void TestMapAndSet() {
  const auto m0 = immer::map<std::string, int>{};
  const auto m1 = m0.set("a", 1).set("b", 2);
  check(m0.empty() && m1.size() == 2 && m1["b"] == 2 && m1.count("c") == 0,
        "map set and lookup");
  const int* b = m1.find("b");
  check(b != nullptr && *b == 2 && m1.find("z") == nullptr, "map find");
  const auto m2 = m1.update("a", [](int x) { return x + 10; }).erase("b");
  check(m2.size() == 1 && m2["a"] == 11 && m1["a"] == 1 && m1.size() == 2,
        "map update and erase");

  const auto s = immer::set<int>{}.insert(3).insert(1).insert(3);
  check(s.size() == 2 && s.count(1) == 1 && s.erase(1).count(1) == 0,
        "set");
}

void TestBoxArrayAtom() {
  const immer::box<std::string> b{"immer"};
  const auto longer = b.update([](std::string s) { return s + "!"; });
  check(*b == "immer" && *longer == "immer!", "box update");

  const auto arr = immer::array<int>{1, 2, 3}.push_back(4).set(0, 0);
  check(arr.size() == 4 && arr[0] == 0 && arr[3] == 4, "array");

  immer::atom<int> counter{0};
  for (int i = 0; i < 5; ++i) counter.update([](int x) { return x + 2; });
  check(*counter.load() == 10, "atom update");
}

}  // namespace

int main() {
  TestVector();
  TestFlexVector();
  TestMapAndSet();
  TestBoxArrayAtom();
  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("immer consumer test passed");
  return 0;
}
