// sfl: small, static, flat, segmented and unordered containers, each checked
// for exact contents; the small, static and segmented vectors also for the
// storage behaviour their documentation (doc/) promises.
#include <sfl/devector.hpp>
#include <sfl/flat_map.hpp>
#include <sfl/segmented_vector.hpp>
#include <sfl/small_unordered_map.hpp>
#include <sfl/small_vector.hpp>
#include <sfl/static_flat_set.hpp>
#include <sfl/static_vector.hpp>

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, int line, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "main.cc:%d: check failed: %s\n", line, what);
    ++failures;
  }
}

#define CHECK(expr) check(static_cast<bool>(expr), __LINE__, #expr)

template <typename Container>
std::vector<int> to_vector(const Container& c) {
  return std::vector<int>(c.begin(), c.end());
}

// Whether p points into the object's own bytes (std::less: a total order).
template <typename T>
bool inside(const T* p, const void* object, std::size_t size) {
  const char* first = static_cast<const char*>(object);
  const char* q = reinterpret_cast<const char*>(p);
  std::less<const char*> before;
  return !before(q, first) && before(q, first + size);
}

}  // namespace

int main() {
  // small_vector keeps up to N elements in the object, then allocates.
  {
    sfl::small_vector<int, 4> v;
    for (int i = 0; i < 4; ++i) {
      v.push_back(i);
    }
    CHECK(v.capacity() == 4 && inside(v.data(), &v, sizeof v));
    for (int i = 4; i < 10; ++i) {
      v.push_back(i);
    }
    CHECK(v.size() == 10 && v.capacity() > 4 && !inside(v.data(), &v, sizeof v));
    CHECK(to_vector(v) == (std::vector<int>{0, 1, 2, 3, 4, 5, 6, 7, 8, 9}));
    v.erase(v.begin() + 2, v.begin() + 8);
    CHECK(to_vector(v) == (std::vector<int>{0, 1, 8, 9}));
  }

  // static_vector has a fixed capacity and never allocates.
  {
    sfl::static_vector<int, 3> v;
    v.emplace_back(7);
    v.emplace_back(8);
    v.emplace(v.begin(), 6);
    CHECK(v.size() == 3 && v.capacity() == 3 && v.full() && v.available() == 0);
    CHECK(to_vector(v) == (std::vector<int>{6, 7, 8}));
    v.pop_back();
    CHECK(!v.full() && v.available() == 1 && inside(v.data(), &v, sizeof v));
  }

  // flat_map keeps its elements sorted by key.
  {
    sfl::flat_map<int, std::string> m;
    m.insert({3, "three"});
    m.insert({1, "one"});
    m.emplace(2, "two");
    CHECK(!m.insert({2, "again"}).second && m.at(2) == "two");
    std::vector<int> keys;
    for (const auto& kv : m) {
      keys.push_back(kv.first);
    }
    CHECK(keys == (std::vector<int>{1, 2, 3}));
    CHECK(m.nth(1)->second == "two" && m.index_of(m.find(3)) == 2);
    CHECK(m.erase(2) == 1 && m.find(2) == m.end() && m.size() == 2);
    m[0] = "zero";
    CHECK(m.begin()->second == "zero" && m.size() == 3);
  }

  // static_flat_set: sorted, fixed capacity.
  {
    sfl::static_flat_set<int, 4> s;
    s.insert(4);
    s.insert(1);
    s.insert(3);
    CHECK(!s.insert(1).second && s.size() == 3 && !s.full());
    s.insert(2);
    CHECK(s.full() && to_vector(s) == (std::vector<int>{1, 2, 3, 4}));
    CHECK(s.count(3) == 1 && s.count(5) == 0);
  }

  // segmented_vector stores elements in segments of N.
  {
    sfl::segmented_vector<int, 4> v;
    for (int i = 0; i < 10; ++i) {
      v.push_back(i);
    }
    // Elements 4 to 7 fill the second segment, contiguously.
    CHECK(v.size() == 10 && v.segment_capacity == 4 && &v[4] + 3 == &v[7]);
    v.insert(v.begin() + 5, 100);
    v.erase(v.begin());
    CHECK(to_vector(v) == (std::vector<int>{1, 2, 3, 4, 100, 5, 6, 7, 8, 9}));
    CHECK(v.at(4) == 100 && v.back() == 9 && v.end() - v.begin() == 10);
  }

  // small_unordered_map: hash map with N elements stored in the object.
  {
    sfl::small_unordered_map<std::string, int, 4> m;
    m.emplace("one", 1);
    m.emplace("two", 2);
    m["three"] = 3;
    CHECK(m.size() == 3 && m.at("two") == 2 && m.find("three")->second == 3);
    CHECK(m.count("four") == 0 && m.erase("one") == 1 && m.size() == 2);
    int sum = 0;
    for (const auto& kv : m) {
      sum += kv.second;
    }
    CHECK(sum == 5);
  }

  // devector grows at both ends.
  {
    sfl::devector<int> d;
    for (int i = 0; i < 3; ++i) {
      d.push_back(i);
      d.push_front(-i - 1);
    }
    CHECK(to_vector(d) == (std::vector<int>{-3, -2, -1, 0, 1, 2}));
    d.pop_front();
    d.pop_back();
    CHECK(to_vector(d) == (std::vector<int>{-2, -1, 0, 1}));
  }

  if (failures != 0) {
    return 1;
  }
  std::printf("sfl ok\n");
  return 0;
}
