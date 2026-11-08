// The consumer: parallel-hashmap's containers through the headers upstream
// installs, at its C++11 floor; parts follow upstream's
// examples/hash_value.h, mt_word_counter.cc and dump_load.cc.
#include <parallel_hashmap/btree.h>
#include <parallel_hashmap/phmap.h>
#include <parallel_hashmap/phmap_dump.h>
#include <parallel_hashmap/phmap_utils.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

static_assert(PHMAP_VERSION_MAJOR == 2 && PHMAP_VERSION_MINOR == 0 && PHMAP_VERSION_PATCH == 0,
              "phmap_config.h reports 2.0.0");

namespace {

bool ok = true;

void check(bool pass, const char* what)
{
  if (!pass) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ok = false;
  }
}

struct Person {
  std::string first;
  std::string last;
  int age;

  bool operator==(const Person& o) const { return first == o.first && last == o.last && age == o.age; }

  friend std::size_t hash_value(const Person& p) { return phmap::HashState().combine(0, p.first, p.last, p.age); }
};

void flat_and_node()
{
  phmap::flat_hash_map<std::string, int> m;
  for (int i = 0; i < 1000; ++i)
    m.emplace("k" + std::to_string(i), i);
  check(m.size() == 1000 && m.find("k500") != m.end() && m.find("k500")->second == 500 && m.count("k1000") == 0,
        "flat_hash_map insert and find");
  std::size_t erased =
    phmap::erase_if(m, [](const phmap::flat_hash_map<std::string, int>::value_type& v) { return v.second % 2 == 0; });
  long sum = 0;
  for (const auto& kv : m)
    sum += kv.second;
  check(erased == 500 && m.size() == 500 && sum == 250000 && !m.contains("k2") && m.contains("k3"), "erase_if");

  // Node containers keep element addresses across rehashes.
  phmap::node_hash_map<int, std::string> nodes;
  nodes[1] = "one";
  const std::string* one = &nodes[1];
  for (int i = 2; i < 10000; ++i)
    nodes[i] = std::to_string(i);
  check(&nodes[1] == one && *one == "one" && nodes.size() == 9999, "node_hash_map pointer stability");

  phmap::flat_hash_set<Person> people;
  Person ada = { "Ada", "Lovelace", 36 };
  Person alan = { "Alan", "Turing", 41 };
  people.insert(ada);
  people.insert(alan);
  people.insert(Person{ "Ada", "Lovelace", 36 });
  check(people.size() == 2 && people.count(alan) == 1 && people.count(Person{ "Ada", "Lovelace", 37 }) == 0,
        "flat_hash_set with a hash_value friend");
  check(phmap::Hash<Person>()(ada) == hash_value(ada), "phmap::Hash uses hash_value");

  phmap::node_hash_set<std::string> names;
  names.insert("x");
  names.insert("y");
  names.erase("x");
  check(names.size() == 1 && names.count("y") == 1, "node_hash_set");
}

// 2^4 submaps, each behind its own std::mutex.
typedef phmap::parallel_flat_hash_map_m<std::string, int> Counts;

void parallel()
{
  check(Counts::subcnt() == 16, "parallel map submap count");

  const std::vector<std::string> words = { "red", "green", "blue", "red", "red", "blue" };
  Counts counts;
  std::vector<std::thread> threads;
  for (int t = 0; t < 4; ++t)
    threads.emplace_back([&]() {
      for (int round = 0; round < 1000; ++round)
        for (const std::string& word : words)
          counts.lazy_emplace_l(
            word, [](Counts::value_type& p) { ++p.second; },
            [&](const Counts::constructor& ctor) { ctor(word, 1); });
    });
  for (std::thread& thread : threads)
    thread.join();

  int red = 0, green = 0, blue = 0;
  bool found = counts.if_contains("red", [&](const Counts::value_type& p) { red = p.second; }) &&
               counts.if_contains("green", [&](const Counts::value_type& p) { green = p.second; }) &&
               counts.if_contains("blue", [&](const Counts::value_type& p) { blue = p.second; });
  check(found && counts.size() == 3 && red == 12000 && green == 4000 && blue == 8000, "lazy_emplace_l from 4 threads");

  check(counts.modify_if("green", [](Counts::value_type& p) { p.second = -1; }) && counts.at("green") == -1 &&
          !counts.modify_if("violet", [](Counts::value_type&) {}),
        "modify_if");
  const std::string violet = "violet";
  int seen = 0;
  check(counts.try_emplace_l(violet, [&](Counts::value_type&) { ++seen; }, 7) &&
          !counts.try_emplace_l(violet, [&](Counts::value_type& p) { ++seen; p.second = 8; }, 9) && seen == 1 &&
          counts.at("violet") == 8,
        "try_emplace_l");
  check(!counts.erase_if("red", [](Counts::value_type& p) { return p.second < 0; }) &&
          counts.erase_if("green", [](Counts::value_type& p) { return p.second < 0; }) && counts.size() == 3 &&
          !counts.contains("green"),
        "erase_if on a key");
  long total = 0;
  counts.for_each([&](const Counts::value_type& p) { total += p.second; });
  check(total == 12000 + 8000 + 8, "for_each");
}

void btree()
{
  phmap::btree_map<int, std::string> m;
  const int keys[] = { 50, 10, 40, 20, 30 };
  for (int k : keys)
    m.emplace(k, std::to_string(k));
  std::string order;
  for (const auto& kv : m)
    order += kv.second + ",";
  check(order == "10,20,30,40,50," && m.lower_bound(25)->first == 30 && m.upper_bound(50) == m.end(),
        "btree_map order and bounds");

  phmap::btree_set<int> s(keys, keys + 5);
  s.erase(30);
  check(s.size() == 4 && *s.begin() == 10 && *s.rbegin() == 50 && s.count(30) == 0, "btree_set");
}

template <class Map>
bool round_trips(const Map& original)
{
  std::stringstream bytes;
  {
    phmap::BinaryOutputArchive out(bytes);
    if (!original.phmap_dump(out))
      return false;
  }
  Map loaded;
  phmap::BinaryInputArchive in(bytes);
  return loaded.phmap_load(in) && loaded == original;
}

void dump_and_load()
{
  phmap::flat_hash_map<std::uint64_t, std::uint32_t> flat;
  phmap::parallel_flat_hash_map<std::uint64_t, std::uint32_t> parallel;
  for (std::uint32_t i = 0; i < 500; ++i) {
    flat[i * 7919u] = i;
    parallel[i * 104729u] = i;
  }
  check(round_trips(flat), "flat_hash_map dump and load");
  check(round_trips(parallel), "parallel_flat_hash_map dump and load");
}

}  // namespace

int main()
{
  flat_and_node();
  parallel();
  btree();
  dump_and_load();
  if (!ok)
    return EXIT_FAILURE;
  std::puts("parallel-hashmap: ok");
  return EXIT_SUCCESS;
}
