#include <pipes/pipes.hpp>

#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

static int failures = 0;

#define CHECK(cond)                                              \
  do {                                                           \
    if (!(cond)) {                                               \
      std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #cond); \
      ++failures;                                                \
    }                                                            \
  } while (0)

int main() {
  // filter and transform into push_back.
  {
    std::vector<int> in{1, 2, 3, 4, 5};
    std::vector<int> out;
    in >>= pipes::filter([](int i) { return i % 2 == 1; }) >>=
        pipes::transform([](int i) { return i * 10; }) >>=
        pipes::push_back(out);
    CHECK((out == std::vector<int>{10, 30, 50}));
  }

  // partition sends each value to one of two outputs.
  {
    std::vector<int> in{1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    std::vector<int> evens, odds;
    in >>= pipes::partition([](int n) { return n % 2 == 0; },
                            pipes::push_back(evens), pipes::push_back(odds));
    CHECK((evens == std::vector<int>{2, 4, 6, 8, 10}));
    CHECK((odds == std::vector<int>{1, 3, 5, 7, 9}));
  }

  // fork sends every value to all outputs.
  {
    std::vector<int> in{1, 2, 3};
    std::vector<int> a, b;
    in >>= pipes::fork(pipes::push_back(a),
                       pipes::transform([](int i) { return i * i; }) >>=
                           pipes::push_back(b));
    CHECK((a == std::vector<int>{1, 2, 3}));
    CHECK((b == std::vector<int>{1, 4, 9}));
  }

  // unzip splits pairs into two outputs.
  {
    std::map<int, std::string> entries{{1, "one"}, {2, "two"}, {3, "three"}};
    std::vector<int> keys;
    std::vector<std::string> values;
    entries >>= pipes::unzip(pipes::push_back(keys), pipes::push_back(values));
    CHECK((keys == std::vector<int>{1, 2, 3}));
    CHECK((values == std::vector<std::string>{"one", "two", "three"}));
  }

  // join flattens a range of ranges.
  {
    std::vector<std::vector<int>> in{{1, 2}, {3, 4}, {5, 6}};
    std::vector<int> out;
    in >>= pipes::join >>= pipes::push_back(out);
    CHECK((out == std::vector<int>{1, 2, 3, 4, 5, 6}));
  }

  // insert into a std::set drops duplicates.
  {
    std::vector<int> in{1, 3, -4, 2, 7, 10, 8, 3};
    std::set<int> out;
    in >>= pipes::insert(out);
    CHECK((out == std::set<int>{-4, 1, 2, 3, 7, 8, 10}));
  }

  // insert into a std::map keeps the first value of a key.
  {
    std::vector<std::string> words{"pipe", "cabin", "map", "pipe"};
    std::map<std::string, int> out;
    words >>= pipes::transform([](const std::string &w) {
                return std::make_pair(w, static_cast<int>(w.size()));
              }) >>= pipes::insert(out);
    CHECK((out == std::map<std::string, int>{{"cabin", 5}, {"map", 3}, {"pipe", 4}}));
  }

  return failures == 0 ? 0 : 1;
}
