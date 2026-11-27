#include <EASTL/algorithm.h>
#include <EASTL/atomic.h>
#include <EASTL/fixed_list.h>
#include <EASTL/fixed_vector.h>
#include <EASTL/hash_map.h>
#include <EASTL/intrusive_list.h>
#include <EASTL/map.h>
#include <EASTL/numeric_limits.h>
#include <EASTL/sort.h>
#include <EASTL/string.h>
#include <EASTL/vector.h>

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

// EASTL's default allocator gets its memory from these two overloads, which
// the application defines (doc/FAQ.md, Info.15), and frees it with
// delete[], as upstream's open-source test allocator pairs them
// (test/source/EASTLTestAllocator.cpp).
void *operator new[](size_t size, const char *, int, unsigned, const char *, int) {
  return ::operator new[](size);
}

void *operator new[](size_t size, size_t alignment, size_t, const char *, int, unsigned,
                     const char *, int) {
  // No type in this test is over-aligned, so ::operator new[] suffices.
  if (alignment > alignof(std::max_align_t)) {
    std::abort();
  }
  return ::operator new[](size);
}

namespace {

int failures = 0;

void check(bool ok, const char *what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    failures++;
  }
}

struct Item {
  int key;
  int order;
};

struct Node : eastl::intrusive_list_node {
  int value;
};

const char *assert_expression = nullptr;
void *assert_context = nullptr;

void on_assert(void *, const char *expression, void *context) {
  assert_expression = expression;
  assert_context = context;
}

} // namespace

int main() {
  // vector and the introsort behind eastl::sort.
  eastl::vector<int> v = {5, 3, 9, 1, 7};
  eastl::sort(v.begin(), v.end());
  check((v == eastl::vector<int>{1, 3, 5, 7, 9}), "sort");
  check(eastl::lower_bound(v.begin(), v.end(), 6) - v.begin() == 3, "lower_bound");

  // tim_sort is stable: equal keys keep their input order.  200 elements
  // take it past its insertion-sort runs into merging.
  Item items[200];
  Item buffer[200];
  for (int i = 0; i < 200; ++i) {
    items[i] = Item{(i * 7) % 10, i};
  }
  eastl::tim_sort_buffer(items, items + 200, buffer,
                         [](const Item &a, const Item &b) { return a.key < b.key; });
  bool stable = items[0].key == 0 && items[0].order == 0 && items[199].key == 9;
  for (int i = 1; i < 200; ++i) {
    stable = stable && (items[i - 1].key < items[i].key ||
                        (items[i - 1].key == items[i].key && items[i - 1].order < items[i].order));
  }
  check(stable, "tim_sort_buffer order and stability");

  // fixed_vector spills into its overflow allocator past its capacity.
  eastl::fixed_vector<int, 4, true> fv;
  for (int i = 1; i <= 6; ++i) {
    fv.push_back(i * 10);
  }
  check(fv.size() == 6 && fv.has_overflowed(), "fixed_vector overflow");
  check(fv[0] == 10 && fv[3] == 40 && fv[5] == 60, "fixed_vector elements");
  eastl::fixed_vector<int, 4, true> small = {1, 2};
  check(!small.has_overflowed() && small.max_size() == 4, "fixed_vector in place");

  // fixed_list draws its nodes from a fixed_pool.
  eastl::fixed_list<int, 3, false> fl;
  fl.push_back(1);
  fl.push_back(2);
  check(!fl.full(), "fixed_list not full");
  fl.push_back(3);
  check(fl.full() && fl.size() == 3 && fl.front() == 1 && fl.back() == 3, "fixed_list full");
  fl.remove(2);
  check(fl.size() == 2 && !fl.full(), "fixed_list remove");

  // string, and UTF-8 to UTF-32 conversion.
  eastl::string s("abc");
  s += "def";
  check(s.size() == 6 && s == "abcdef", "string append");
  check(s.find("cd") == 2 && s.substr(1, 3) == "bcd", "string find and substr");
  s.make_upper();
  check(s == "ABCDEF", "string make_upper");
  eastl::u32string u(eastl::u32string::CtorConvert(), "h\xc3\xa9\xe2\x82\xac");
  check(u.size() == 3 && u[0] == U'h' && u[1] == 0xE9 && u[2] == 0x20AC, "UTF-8 to UTF-32");

  // map keeps its keys in a red-black tree; descending inserts make it
  // rebalance, and validate() checks the colouring.
  eastl::map<int, eastl::string> m;
  for (int i = 20; i >= 1; --i) {
    m[i] = eastl::string(1, static_cast<char>('a' + i - 1));
  }
  eastl::string joined;
  for (const auto &kv : m) {
    joined += kv.second;
  }
  check(joined == "abcdefghijklmnopqrst" && m.validate(), "map order");
  for (int i = 2; i <= 20; i += 2) {
    m.erase(i);
  }
  joined.clear();
  for (const auto &kv : m) {
    joined += kv.second;
  }
  check(m.size() == 10 && joined == "acegikmoqs" && m.find(2) == m.end() && m.validate(),
        "map erase");

  // hash_map.
  eastl::hash_map<int, int> h;
  for (int i = 0; i < 100; ++i) {
    h[i] = i * i;
  }
  check(h.size() == 100 && h.find(7)->second == 49 && h.find(100) == h.end(), "hash_map find");
  check(h.erase(7) == 1 && h.count(7) == 0 && h.size() == 99, "hash_map erase");
  check(h.validate() && h.bucket_count() >= 99, "hash_map buckets");

  // intrusive_list links caller-owned nodes.
  Node nodes[3];
  eastl::intrusive_list<Node> il;
  for (int i = 0; i < 3; ++i) {
    nodes[i].value = i + 1;
    il.push_back(nodes[i]);
  }
  il.reverse();
  check(il.size() == 3 && il.front().value == 3 && il.back().value == 1 && il.validate(),
        "intrusive_list reverse");

  // Binding a reference odr-uses the member numeric_limits.cpp defines.
  const int &digits = eastl::numeric_limits<int>::digits;
  check(digits == 31 && eastl::numeric_limits<unsigned char>::max() == 255, "numeric_limits");

  eastl::atomic<int> counter(5);
  check(counter.fetch_add(3) == 5 && counter.load(eastl::memory_order_acquire) == 8 &&
            counter.exchange(1) == 8 && counter.load() == 1,
        "atomic");

  // A user handler replaces the default assertion failure function, which
  // would break into the debugger.
  int context = 0;
  eastl::SetAssertionFailureFunction(&on_assert, &context);
  eastl::AssertionFailure("probe assertion");
  check(assert_expression != nullptr && std::strcmp(assert_expression, "probe assertion") == 0 &&
            assert_context == &context,
        "assertion handler");

  if (failures == 0) {
    std::puts("eastl: all checks passed");
  }
  return failures == 0 ? 0 : 1;
}
