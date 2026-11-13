#include <gtl/bit_vector.hpp>
#include <gtl/btree.hpp>
#include <gtl/intrusive.hpp>
#include <gtl/lru_cache.hpp>
#include <gtl/memoize.hpp>
#include <gtl/phmap.hpp>
#include <gtl/phmap_dump.hpp>
#include <gtl/soa.hpp>
#include <gtl/utils.hpp>
#include <gtl/vec_utils.hpp>
#include <gtl/vector.hpp>

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <random>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char* what, int line) {
  if (!ok) {
    std::fprintf(stderr, "main.cc:%d: check failed: %s\n", line, what);
    ++failures;
  }
}

#define CHECK(...) check((__VA_ARGS__), #__VA_ARGS__, __LINE__)

void test_hash_maps() {
  gtl::flat_hash_map<std::string, int> ages = {{"tom", 31}, {"jeff", 42}};
  ages["bill"] = 55;
  ages.emplace("jim", 27);
  CHECK(ages.size() == 4);
  CHECK(ages.at("jeff") == 42);
  CHECK(ages.contains("bill"));
  CHECK(ages.erase("tom") == 1);
  CHECK(ages.find("tom") == ages.end());
  int sum = 0;
  for (const auto& [name, age] : ages) sum += age;
  CHECK(sum == 42 + 55 + 27);

  gtl::flat_hash_set<int> evens;
  for (int i = 0; i < 1000; i += 2) evens.insert(i);
  CHECK(evens.size() == 500 && evens.count(998) == 1 && evens.count(999) == 0);

  // A node map keeps references stable across rehashing.
  gtl::node_hash_map<int, std::string> names;
  names[1] = "one";
  const std::string* first = &names[1];
  for (int i = 2; i < 1000; ++i) names[i] = std::to_string(i);
  CHECK(names.size() == 999 && first == &names[1] && *first == "one");
}

void test_parallel_map() {
  gtl::parallel_flat_hash_map_m<int, int> counts;
  std::vector<std::thread> workers;
  for (int t = 0; t < 4; ++t)
    workers.emplace_back([&counts] {
      for (int i = 0; i < 1000; ++i) counts.try_emplace_l(i % 10, [](auto& v) { ++v.second; }, 1);
    });
  for (auto& w : workers) w.join();

  CHECK(counts.size() == 10);
  int total = 0;
  for (int k = 0; k < 10; ++k) {
    int value = 0;
    CHECK(counts.if_contains(k, [&](const auto& v) { value = v.second; }) && value == 400);
    total += value;
  }
  CHECK(total == 4000);
  CHECK(counts.modify_if(3, [](auto& v) { v.second = -1; }));
  CHECK(counts.erase_if(3, [](auto& v) { return v.second == -1; }));
  CHECK(!counts.contains(3) && counts.size() == 9);
  CHECK(!counts.erase_if(4, [](auto& v) { return v.second < 0; }) && counts.contains(4));
}

void test_btree() {
  gtl::btree_map<int, std::string> squares;
  for (int i : {5, 1, 4, 2, 3}) squares.emplace(i, std::to_string(i * i));
  std::string order;
  for (const auto& [k, v] : squares) order += v + ",";
  CHECK(order == "1,4,9,16,25,");
  CHECK(squares.lower_bound(3)->first == 3);
  CHECK(squares.upper_bound(3)->first == 4);
  CHECK(squares.erase(1) == 1 && squares.begin()->first == 2);

  gtl::btree_set<std::string> words{"pear", "apple", "fig"};
  CHECK(words.size() == 3 && *words.begin() == "apple" && *words.rbegin() == "pear");
}

void test_bit_vector() {
  gtl::bit_vector bits(16);
  CHECK((std::string)bits == "0x0000");
  bits.set(0).set(4).set(8);
  CHECK((std::string)bits == "0x0111");
  CHECK(bits.count() == 3 && bits[4] && !bits[5]);
  bits.flip();
  CHECK(bits.count() == 13 && (std::string)bits == "0xfeee");

  gtl::bit_vector byte(8);
  byte = {42};
  CHECK(byte.to_string() == "00101010");
}

void test_vector() {
  gtl::vector<std::string> v;
  for (int i = 0; i < 100; ++i) v.push_back(std::to_string(i));
  v.insert(v.begin(), "first");
  v.erase(v.begin() + 1);
  CHECK(v.size() == 100 && v.front() == "first" && v[1] == "1" && v.back() == "99");
}

std::uint64_t fib(std::uint64_t n) {
  static auto cached = gtl::memoize<decltype(&fib)>(&fib);
  return n <= 1 ? n : cached(n - 1) + cached(n - 2);
}

void test_caches() {
  CHECK(fib(90) == 2880067194370816120ull);

  gtl::lru_cache<int, int> cache(50);
  for (int i = 0; i < 100; ++i) cache.insert(i, i * 2);
  CHECK(cache.size() == 50);
  CHECK(!cache.exists(49) && cache.exists(50));
  CHECK(cache.get(99) == 198);
  CHECK(!cache.get(0).has_value());
  // get() makes 50 the most recent entry, so the next insert evicts 51.
  CHECK(cache.get(50) == 100);
  cache.insert(100, 200);
  CHECK(cache.size() == 50 && cache.exists(50) && !cache.exists(51) && cache.exists(100));
}

struct Node : gtl::intrusive_ref_counter<Node, gtl::thread_safe_counter> {
  Node(int v, int& live) : value(v), live(live) { ++live; }
  ~Node() { --live; }
  int value;
  int& live;
};

void test_intrusive() {
  int live = 0;
  {
    gtl::intrusive_ptr<Node> a(new Node(7, live));
    CHECK(a->use_count() == 1);
    {
      gtl::intrusive_ptr<Node> b = a;
      CHECK(a->use_count() == 2 && b->value == 7);
    }
    CHECK(a->use_count() == 1 && live == 1);
  }
  CHECK(live == 0);
}

void test_soa() {
  gtl::soa<int, std::string> people;
  people.insert(3, std::string("carol"));
  people.insert(1, std::string("alice"));
  people.insert(2, std::string("bob"));
  people.sort_by_field<0>();
  const auto& ids = people.get_column<0>();
  const auto& names = people.get_column<1>();
  CHECK(people.size() == 3 && ids[0] == 1 && ids[2] == 3);
  CHECK(names[0] == "alice" && names[1] == "bob" && names[2] == "carol");
}

int square(int x) { return x * x; }

void test_utilities() {
  std::vector<int> a{0, 1, 2};
  std::vector<int> b{3, 4};
  CHECK(gtl::cat(a, b) == std::vector<int>{0, 1, 2, 3, 4});
  std::vector<int> ten{0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
  CHECK(gtl::slice(ten, 2, 8, 2) == std::vector<int>{2, 4, 6});
  CHECK(gtl::map([](int x) { return x * 10; }, b) == std::vector<int>{30, 40});

  int retries = 3;
  {
    gtl::scoped_set_value<int> rollback(retries, 7);
    CHECK(retries == 7);
  }
  CHECK(retries == 3);
  CHECK(gtl::change(retries, 4) && retries == 4 && !gtl::change(retries, 4));

  gtl::mt_memoize<decltype(&square)> squares(&square);
  CHECK(squares(7) == 49 && squares.contains(7) == 49 && !squares.contains(8).has_value() && squares.size() == 1);

  // 64 locked submaps share the capacity, so eviction is approximate.
  gtl::mt_lru_cache<int, int> shared(5000);
  for (int i = 0; i < 10000; ++i) shared.insert(i, i);
  CHECK(!shared.exists(0) && shared.exists(9999) && shared.get(9999) == 9999);
}

void test_dump() {
  // The archives take a narrow path, so they get a plain relative name inside
  // a directory of this run's own: create_directory() returns false if the
  // name is taken.
  std::random_device entropy;
  std::filesystem::path dir;
  do {
    dir = std::filesystem::temp_directory_path() / ("cabin-gtl-test-" + std::to_string(entropy()));
  } while (!std::filesystem::create_directory(dir));
  const std::filesystem::path previous = std::filesystem::current_path();
  std::filesystem::current_path(dir);

  gtl::flat_hash_map<std::uint64_t, std::uint32_t> before = {{100, 99}, {300, 299}, {101, 992}};
  {
    gtl::BinaryOutputArchive out("map.bin");
    CHECK(before.phmap_dump(out));
  }
  gtl::flat_hash_map<std::uint64_t, std::uint32_t> after;
  {
    gtl::BinaryInputArchive in("map.bin");
    CHECK(after.phmap_load(in));
  }
  CHECK(after.size() == 3 && after == before);

  std::filesystem::current_path(previous);
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
}

}  // namespace

int main() {
  test_hash_maps();
  test_parallel_map();
  test_btree();
  test_bit_vector();
  test_vector();
  test_caches();
  test_intrusive();
  test_soa();
  test_utilities();
  test_dump();
  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("gtl 1.3.1 ok");
  return 0;
}
