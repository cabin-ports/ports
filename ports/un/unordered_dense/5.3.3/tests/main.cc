#include <ankerl/unordered_dense.h>

#include <cstdint>
#include <cstdio>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

template <class Table>
std::vector<typename Table::key_type> Keys(Table const& table) {
  std::vector<typename Table::key_type> keys;
  for (auto const& entry : table.values()) {
    if constexpr (std::is_same_v<typename Table::key_type,
                                 typename Table::value_type>) {
      keys.push_back(entry);
    } else {
      keys.push_back(entry.first);
    }
  }
  return keys;
}

void TestMap() {
  ankerl::unordered_dense::map<std::string, int> map;
  map["one"] = 1;
  map.emplace("two", 2);
  map.insert({"three", 3});
  check(!map.try_emplace("two", 22).second && map.at("two") == 2,
        "try_emplace keeps the existing value");
  check(!map.insert_or_assign("three", 33).second && map.at("three") == 33,
        "insert_or_assign overwrites");
  map["four"] = 4;

  using Entries = std::vector<std::pair<std::string, int>>;
  check(map.values() == Entries{{"one", 1}, {"two", 2}, {"three", 33},
                                {"four", 4}},
        "values are dense, in insertion order");
  check(map.size() == 4 && map.contains("one") && !map.contains("five") &&
            map.count("four") == 1 && map.find("five") == map.end(),
        "lookups");

  // Erasing moves the last value into the hole.
  check(map.erase("two") == 1 && map.erase("two") == 0, "erase by key");
  check(Keys(map) == std::vector<std::string>{"one", "four", "three"},
        "erase moves the last value into the hole");
  auto next = map.erase(map.find("one"));
  check(next != map.end() && next->first == "three" &&
            Keys(map) == std::vector<std::string>{"three", "four"},
        "erase by iterator returns the moved value");
  check(map.at("three") == 33 && map.at("four") == 4, "values survive erase");
}

void TestSet() {
  ankerl::unordered_dense::set<int> set;
  for (int v : {10, 20, 30, 40, 50}) {
    check(set.insert(v).second, "set insert");
  }
  check(!set.insert(30).second && set.size() == 5, "duplicates are rejected");
  check(set.erase(20) == 1 &&
            Keys(set) == std::vector<int>{10, 50, 30, 40},
        "set erase moves the last value into the hole");
  int sum = 0;
  for (int v : set) {
    sum += v;
  }
  check(sum == 130, "set iteration");
}

struct Point {
  int x;
  int y;
  bool operator==(Point const& other) const {
    return x == other.x && y == other.y;
  }
};

int hash_calls = 0;

// No is_avalanching marker, so the table mixes the result further.
struct PointHash {
  std::uint64_t operator()(Point const& p) const noexcept {
    ++hash_calls;
    return static_cast<std::uint64_t>(p.x) * 1000003u +
           static_cast<std::uint64_t>(p.y);
  }
};

// Every key collides.
struct ConstantHash {
  using is_avalanching = void;
  std::uint64_t operator()(Point const&) const noexcept { return 7; }
};

template <class Hash>
bool PointsWork() {
  ankerl::unordered_dense::map<Point, int, Hash> map;
  for (int i = 0; i < 200; ++i) {
    map[Point{i, -i}] = i * i;
  }
  for (int i = 0; i < 200; i += 2) {
    if (map.erase(Point{i, -i}) != 1) {
      return false;
    }
  }
  if (map.size() != 100) {
    return false;
  }
  for (int i = 0; i < 200; ++i) {
    auto it = map.find(Point{i, -i});
    bool const expected = i % 2 == 1;
    if ((it != map.end()) != expected || (expected && it->second != i * i)) {
      return false;
    }
  }
  return true;
}

void TestCustomHash() {
  check(PointsWork<PointHash>() && hash_calls >= 500, "custom hash is used");
  check(PointsWork<ConstantHash>(), "fully colliding hash");
}

void TestSegmentedMap() {
  ankerl::unordered_dense::segmented_map<int, std::string> map;
  std::string& first = map[0];
  first = "zero";
  for (int i = 1; i < 10000; ++i) {
    map[i] = std::to_string(i);
  }
  check(&map.find(0)->second == &first && first == "zero",
        "segmented_map keeps references on insert");
  check(map.size() == 10000 && map.at(9999) == "9999" &&
            map.at(1234) == "1234",
        "segmented_map contents");
  check(map.erase(0) == 1 && map.begin()->first == 9999,
        "segmented_map erase moves the last value");
}

}  // namespace

int main() {
  TestMap();
  TestSet();
  TestCustomHash();
  TestSegmentedMap();
  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("unordered_dense consumer test passed");
  return 0;
}
