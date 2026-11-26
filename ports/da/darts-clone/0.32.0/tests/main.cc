#include <darts.h>

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <string>

static int failures = 0;

#define CHECK(cond)                                                    \
  do {                                                                 \
    if (!(cond)) {                                                     \
      std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__,      \
                   __LINE__, #cond);                                   \
      ++failures;                                                      \
    }                                                                  \
  } while (0)

typedef Darts::DoubleArray::result_pair_type Pair;

static void check_lookups(const Darts::DoubleArray &da) {
  CHECK(da.exactMatchSearch<int>("a") == 10);
  CHECK(da.exactMatchSearch<int>("ab") == 20);
  CHECK(da.exactMatchSearch<int>("abc") == 30);
  CHECK(da.exactMatchSearch<int>("b") == 40);
  CHECK(da.exactMatchSearch<int>("xyz") == 50);
  CHECK(da.exactMatchSearch<int>("abd") == -1);
  CHECK(da.exactMatchSearch<int>("xy") == -1);
  CHECK(da.exactMatchSearch<int>("") == -1);
  CHECK(da.exactMatchSearch<int>("abcd") == -1);

  Pair r[8];
  CHECK(da.commonPrefixSearch("abcd", r, 8) == 3);
  CHECK(r[0].value == 10 && r[0].length == 1);
  CHECK(r[1].value == 20 && r[1].length == 2);
  CHECK(r[2].value == 30 && r[2].length == 3);
  CHECK(da.commonPrefixSearch("bz", r, 8) == 1);
  CHECK(r[0].value == 40 && r[0].length == 1);
  CHECK(da.commonPrefixSearch("zzz", r, 8) == 0);

  Pair longest = da.commonLongestPrefixSearch<Pair>("abx");
  CHECK(longest.value == 20 && longest.length == 2);
  longest = da.commonLongestPrefixSearch<Pair>("zz");
  CHECK(longest.value == -1 && longest.length == 0);

  // traverse() walks one transition at a time and keeps its position.
  std::size_t node = 0, pos = 0;
  CHECK(da.traverse("abc", node, pos, 1) == 10 && pos == 1);
  CHECK(da.traverse("abc", node, pos, 2) == 20 && pos == 2);
  CHECK(da.traverse("abc", node, pos, 3) == 30 && pos == 3);
  node = 0;
  pos = 0;
  CHECK(da.traverse("xy", node, pos) == -1 && pos == 2);
  node = 0;
  pos = 0;
  CHECK(da.traverse("abz", node, pos) == -2 && pos == 2);
}

int main() {
  const char *keys[] = {"a", "ab", "abc", "b", "xyz"};
  const int values[] = {10, 20, 30, 40, 50};

  Darts::DoubleArray da;
  CHECK(da.build(5, keys, NULL, values) == 0);
  CHECK(da.size() > 0);
  CHECK(da.unit_size() == 4);
  check_lookups(da);

  // Without values, each key's value is its index.
  Darts::DoubleArray indexed;
  CHECK(indexed.build(5, keys) == 0);
  CHECK(indexed.exactMatchSearch<int>("a") == 0);
  CHECK(indexed.exactMatchSearch<int>("abc") == 2);
  CHECK(indexed.exactMatchSearch<int>("xyz") == 4);

  // save() and open() round trip.
  // In the probe's build outputs: `cabin run` keeps the caller's directory.
  const char *dir = std::getenv("CABIN_PROFILE_DIR");
  CHECK(dir != NULL);
  const std::string path = std::string(dir != NULL ? dir : ".") +
                           "/port-probe-darts-clone.tmp";
  const char *file = path.c_str();
  CHECK(da.save(file) == 0);
  Darts::DoubleArray loaded;
  CHECK(loaded.open(file) == 0);
  CHECK(loaded.size() == da.size());
  check_lookups(loaded);
  std::remove(file);

  // set_array() uses the caller's memory without copying it.
  Darts::DoubleArray borrowed;
  borrowed.set_array(da.array(), da.size());
  CHECK(borrowed.size() == da.size());
  check_lookups(borrowed);

  // Unsorted keys are rejected with an exception.
  const char *unsorted[] = {"b", "a"};
  Darts::DoubleArray bad;
  bool threw = false;
  try {
    bad.build(2, unsorted);
  } catch (const std::exception &) {
    threw = true;
  }
  CHECK(threw);

  return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
