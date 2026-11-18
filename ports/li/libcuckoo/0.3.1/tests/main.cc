#include <libcuckoo/cuckoohash_map.hh>

#include <cstddef>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

// The C wrapper is a template: one C++ file instantiates it with C linkage.
extern "C" {
#define CUCKOO_TABLE_NAME int_table
#define CUCKOO_KEY_TYPE int
#define CUCKOO_MAPPED_TYPE double
#include <libcuckoo-c/cuckoo_table_template.h>

static void add_one(double* v) { *v += 1.0; }
}
#include <libcuckoo-c/cuckoo_table_template.cc>

namespace {

int failures = 0;

void check(bool ok, const char* what, int line) {
  if (!ok) {
    std::fprintf(stderr, "main.cc:%d: check failed: %s\n", line, what);
    ++failures;
  }
}

#define CHECK(...) check((__VA_ARGS__), #__VA_ARGS__, __LINE__)

void test_basic() {
  libcuckoo::cuckoohash_map<int, std::string> m;
  CHECK(m.empty());
  for (int i = 0; i < 100; ++i) CHECK(m.insert(i, std::to_string(i)));
  CHECK(!m.insert(5, "five") && m.find(5) == "5");
  CHECK(m.size() == 100 && m.contains(99) && !m.contains(100));
  std::string out;
  CHECK(m.find(42, out) && out == "42" && !m.find(-1, out));
  bool threw = false;
  try {
    m.find(-1);
  } catch (const std::out_of_range&) {
    threw = true;
  }
  CHECK(threw);

  CHECK(m.update(7, "seven") && m.find(7) == "seven" && !m.update(-1, "x"));
  CHECK(!m.insert_or_assign(7, "SEVEN") && m.find(7) == "SEVEN");
  CHECK(!m.upsert(1, [](std::string& v) { v += "!"; }, "new") && m.find(1) == "1!");
  CHECK(m.upsert(1000, [](std::string& v) { v += "!"; }, "new") && m.find(1000) == "new");
  CHECK(m.update_fn(2, [](std::string& v) { v = "two"; }) && m.find(2) == "two");
  // uprase_fn erases an existing entry when its function returns true.
  CHECK(!m.uprase_fn(3, [](std::string&) { return true; }) && !m.contains(3));
  CHECK(m.erase_fn(4, [](std::string& v) { return v == "nope"; }) && m.contains(4));
  CHECK(m.erase(4) && !m.contains(4) && !m.erase(4));
  CHECK(m.size() == 99);
  const auto copy = m;
  m.clear();
  CHECK(copy.size() == 99 && copy.find(42) == "42" && copy.find(1000) == "new");
  CHECK(m.empty());
}

void test_concurrent() {
  // A small table, so the writers also race through resizes.
  libcuckoo::cuckoohash_map<int, long> counts(16);
  std::vector<std::thread> workers;
  for (int t = 0; t < 4; ++t)
    workers.emplace_back([&counts, t] {
      for (int i = 0; i < 5000; ++i) {
        counts.insert(t * 5000 + i, static_cast<long>(i));
        counts.upsert(-(i % 10) - 1, [](long& v) { ++v; }, 1L);
      }
    });
  for (auto& w : workers) w.join();

  CHECK(counts.size() == 20010);
  long shared = 0;
  for (int k = 1; k <= 10; ++k) shared += counts.find(-k);
  CHECK(shared == 20000 && counts.find(-3) == 2000);
  CHECK(counts.find(19999) == 4999 && counts.find(0) == 0);
}

void test_resize() {
  libcuckoo::cuckoohash_map<int, int> m(8);
  // Explicit resizes split their work across these extra threads.
  m.max_num_worker_threads(3);
  CHECK(m.max_num_worker_threads() == 3);
  for (int i = 0; i < 1000; ++i) m.insert(i, -i);
  const std::size_t hp = m.hashpower();
  CHECK(m.rehash(hp + 2) && m.hashpower() == hp + 2);
  CHECK(m.bucket_count() == (std::size_t{1} << (hp + 2)) && m.capacity() == m.bucket_count() * 4);
  CHECK(m.size() == 1000 && m.find(500) == -500);
  CHECK(m.reserve(100000) && m.capacity() >= 100000 && m.find(999) == -999);

  m.maximum_hashpower(m.hashpower());
  std::size_t refused = 0;
  try {
    m.rehash(m.hashpower() + 1);
  } catch (const libcuckoo::maximum_hashpower_exceeded& e) {
    refused = e.hashpower();
  }
  CHECK(refused == m.hashpower() + 1 && m.size() == 1000);
}

void test_locked_table() {
  libcuckoo::cuckoohash_map<std::string, int> m;
  m.insert("a", 1);
  m.insert("b", 2);
  m.insert("c", 3);
  {
    auto lt = m.lock_table();
    CHECK(lt.is_active() && lt.size() == 3);
    int sum = 0;
    for (const auto& kv : lt) sum += kv.second;
    CHECK(sum == 6);
    for (auto& kv : lt) kv.second *= 10;
    CHECK(lt.insert("d", 4).second && !lt.insert("d", 5).second);
    CHECK(lt.erase("a") == 1 && lt.count("a") == 0);
    CHECK(lt.find("b")->second == 20 && lt.find("a") == lt.end());
    lt.unlock();
    CHECK(!lt.is_active());
  }
  CHECK(m.size() == 3 && m.find("c") == 30 && m.find("d") == 4);
}

void test_c_wrapper() {
  int_table* t = int_table_init(0);
  for (int i = 0; i < 50; ++i) {
    const double v = i * 0.5;
    CHECK(int_table_insert(t, &i, &v));
  }
  const int key = 10;
  double out = 0;
  CHECK(int_table_size(t) == 50 && int_table_find(t, &key, &out) && out == 5.0);
  const double ignored = -1;
  CHECK(!int_table_upsert(t, &key, add_one, &ignored) && int_table_find(t, &key, &out) && out == 6.0);
  CHECK(int_table_erase(t, &key) && !int_table_contains(t, &key) && int_table_size(t) == 49);

  int_table_locked_table* lt = int_table_lock_table(t);
  int_table_iterator* it = int_table_locked_table_begin(lt);
  int_table_iterator* end = int_table_locked_table_end(lt);
  int count = 0;
  double sum = 0;
  bool erased_key_seen = false;
  for (; !int_table_iterator_equal(it, end); int_table_iterator_increment(it)) {
    ++count;
    sum += *int_table_iterator_mapped(it);
    erased_key_seen = erased_key_seen || *int_table_iterator_key(it) == key;
  }
  // The values are halves, so the sum is exact: 0.5 * (0 + ... + 49) - 5.
  CHECK(count == 49 && sum == 607.5 && !erased_key_seen);
  int_table_iterator_free(it);
  int_table_iterator_free(end);
  int_table_locked_table_free(lt);
  int_table_free(t);
}

}  // namespace

int main() {
  test_basic();
  test_concurrent();
  test_resize();
  test_locked_table();
  test_c_wrapper();
  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("libcuckoo 0.3.1 ok");
  return 0;
}
