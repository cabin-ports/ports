// naive-tsearch from C++: tsearch.h declares the functions extern "C".  As in
// main.c, the header must be the package's, and the tree shape and tdestroy
// tell this package's functions from the C library's.
#include <search.h>
#ifndef NAIVE_TSEARCH_SEARCH_H_
#error "<search.h> is not naive-tsearch's"
#endif

#include <cstdio>
#include <string>
#include <utility>
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

int compare_names(const void* a, const void* b) {
  return static_cast<const std::string*>(a)->compare(
      *static_cast<const std::string*>(b));
}

const std::string& node_name(const void* node) {
  return **static_cast<const std::string* const*>(node);
}

std::vector<std::pair<std::string, int>> visits;

void record(const void* node, VISIT which, int depth) {
  if (which == postorder || which == leaf) {
    visits.emplace_back(node_name(node), depth);
  }
}

int freed = 0;

void count_free(void*) { ++freed; }

}  // namespace

int main() {
  std::vector<std::string> names{"alpha", "bravo", "charlie", "delta"};
  void* root = nullptr;

  // Ascending inserts make a right-leaning chain.
  for (const auto& name : names) {
    void* node = tsearch(&name, &root, compare_names);
    CHECK(node != nullptr && &node_name(node) == &name);
  }
  const std::string bravo = "bravo";
  void* found = tfind(&bravo, &root, compare_names);
  CHECK(found != nullptr && &node_name(found) == &names[1]);
  const std::string echo = "echo";
  CHECK(tfind(&echo, &root, compare_names) == nullptr);

  twalk(root, record);
  CHECK((visits == std::vector<std::pair<std::string, int>>{
                       {"alpha", 0}, {"bravo", 1}, {"charlie", 2}, {"delta", 3}}));

  tdelete(&bravo, &root, compare_names);  // returns a freed node: unused
  CHECK(tfind(&bravo, &root, compare_names) == nullptr);
  visits.clear();
  twalk(root, record);
  CHECK((visits == std::vector<std::pair<std::string, int>>{
                       {"alpha", 0}, {"charlie", 1}, {"delta", 2}}));

  tdestroy(root, count_free);
  CHECK(freed == 3);

  if (failures != 0) {
    return 1;
  }
  std::printf("naive-tsearch ok (C++)\n");
  return 0;
}
