// The C++ consumer: tsl::robin_map and tsl::robin_set in C++17 - lookup,
// insertion and erasure (also while iterating, and among entries displaced
// from their ideal bucket), copies, growth past the load factor, the prime
// growth policy under an identity hash whose low bits are all zero,
// heterogeneous lookup, precalculated and stored hashes, a move-only value,
// and a serialize/deserialize round trip.
#include <tsl/robin_map.h>
#include <tsl/robin_set.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

static int failures = 0;

#define CHECK(cond)                                                   \
  do {                                                                \
    if (!(cond)) {                                                    \
      std::fprintf(stderr, "main.cc:%d: check failed: %s\n", __LINE__, \
                   #cond);                                            \
      ++failures;                                                     \
    }                                                                 \
  } while (0)

// A hash that accepts std::string and std::string_view alike.
struct TransparentHash {
  using is_transparent = void;
  std::size_t operator()(std::string_view s) const {
    return std::hash<std::string_view>()(s);
  }
};

// Every key is a multiple of 1024, so this hash leaves the low ten bits
// zero: the case upstream recommends the prime growth policy for.
struct IdentityHash {
  std::size_t operator()(std::uint64_t key) const {
    return static_cast<std::size_t>(key);
  }
};

template <class T>
struct is_pair : std::false_type {};
template <class A, class B>
struct is_pair<std::pair<A, B>> : std::true_type {};

// Writes and reads values as raw bytes, pairs member by member.
struct Serializer {
  std::vector<unsigned char> bytes;
  template <class U>
  void operator()(const U& value) {
    if constexpr (is_pair<U>::value) {
      (*this)(value.first);
      (*this)(value.second);
    } else {
      static_assert(std::is_arithmetic<U>::value, "arithmetic only");
      const unsigned char* p = reinterpret_cast<const unsigned char*>(&value);
      bytes.insert(bytes.end(), p, p + sizeof(U));
    }
  }
};

struct Deserializer {
  const std::vector<unsigned char>& bytes;
  std::size_t at = 0;
  template <class U>
  U operator()() {
    if constexpr (is_pair<U>::value) {
      auto first = (*this).template operator()<typename U::first_type>();
      auto second = (*this).template operator()<typename U::second_type>();
      return U(first, second);
    } else {
      U value;
      std::memcpy(&value, bytes.data() + at, sizeof(U));
      at += sizeof(U);
      return value;
    }
  }
};

static void basic_map() {
  tsl::robin_map<std::string, int> map = {{"one", 1}, {"two", 2}};
  CHECK(map.size() == 2);
  CHECK(map.at("one") == 1);
  map["three"] = 3;
  CHECK(map.count("three") == 1 && map["three"] == 3);
  CHECK(!map.insert({"one", 100}).second && map.at("one") == 1);
  CHECK(map.insert_or_assign("one", 11).second == false && map.at("one") == 11);
  CHECK(map.find("four") == map.end());

  bool threw = false;
  try {
    (void)map.at("four");
  } catch (const std::out_of_range&) {
    threw = true;
  }
  CHECK(threw);

  // The iterator's pair is const; value() gives the mutable mapped value.
  for (auto it = map.begin(); it != map.end(); ++it) it.value() *= 10;
  CHECK(map.at("one") == 110 && map.at("two") == 20 && map.at("three") == 30);

  CHECK(map.erase("two") == 1 && map.erase("two") == 0);
  CHECK(map.size() == 2 && map.count("two") == 0);
  const auto copy = map;
  map.clear();
  CHECK(map.empty() && copy.size() == 2 && copy.at("three") == 30);
}

static void growth_and_erase_while_iterating() {
  tsl::robin_map<int, int> map;
  for (int i = 0; i < 10000; ++i) map.emplace(i, i * i);
  CHECK(map.size() == 10000);
  CHECK(map.load_factor() <= map.max_load_factor());
  // The default policy keeps a power-of-two bucket count.
  CHECK((map.bucket_count() & (map.bucket_count() - 1)) == 0);

  bool all = true;
  for (int i = 0; i < 10000; ++i) {
    auto it = map.find(i);
    all = all && it != map.end() && it->second == i * i;
  }
  CHECK(all);

  for (auto it = map.begin(); it != map.end();) {
    if (it->first % 2 != 0)
      it = map.erase(it);
    else
      ++it;
  }
  CHECK(map.size() == 5000);
  bool even = true;
  for (const auto& kv : map) even = even && kv.first % 2 == 0;
  CHECK(even && map.count(4242) == 1 && map.count(4243) == 0);

  map.rehash(0);
  CHECK(map.size() == 5000 && map.at(9998) == 9998 * 9998);
}

// Many of these keys sit displaced from their ideal bucket; erasing must
// shift the entries after a hole back for lookups to keep finding them.
static void erase_shifts_back() {
  tsl::robin_map<std::string, int> map;
  for (int i = 0; i < 5000; ++i) map.emplace(std::to_string(i), i);
  for (int i = 0; i < 5000; i += 3) map.erase(std::to_string(i));
  bool found = true;
  for (int i = 0; i < 5000; ++i)
    found = found && map.count(std::to_string(i)) == (i % 3 != 0 ? 1u : 0u);
  CHECK(found && map.size() == 3333);
}

static void prime_growth_policy() {
  tsl::robin_pg_map<std::uint64_t, int, IdentityHash> map;
  for (int i = 0; i < 2000; ++i)
    map.emplace(static_cast<std::uint64_t>(i) * 1024, i);
  CHECK(map.size() == 2000);
  CHECK(map.bucket_count() % 2 != 0);  // never a power of two
  bool all = true;
  for (int i = 0; i < 2000; ++i)
    all = all && map.at(static_cast<std::uint64_t>(i) * 1024) == i;
  CHECK(all && map.count(1) == 0);
}

static void set_and_heterogeneous_lookup() {
  tsl::robin_set<std::string, TransparentHash, std::equal_to<>> set;
  CHECK(set.insert("alpha").second && set.insert("beta").second);
  CHECK(!set.insert("alpha").second && set.size() == 2);
  const std::string_view key = "beta";
  CHECK(set.find(key) != set.end() && *set.find(key) == "beta");
  CHECK(set.count(std::string_view("gamma")) == 0);
  CHECK(set.erase(std::string_view("alpha")) == 1 && set.size() == 1);
}

static void precalculated_and_stored_hash() {
  using Map = tsl::robin_map<std::string, int, std::hash<std::string>,
                             std::equal_to<std::string>,
                             std::allocator<std::pair<std::string, int>>,
                             /*StoreHash=*/true>;
  Map map;
  for (int i = 0; i < 1000; ++i) map.emplace("key" + std::to_string(i), i);
  map.rehash(4096);
  const std::string key = "key617";
  const std::size_t hash = map.hash_function()(key);
  CHECK(map.find(key, hash) != map.end() && map.at(key, hash) == 617);
  CHECK(map.count("key1000", map.hash_function()("key1000")) == 0);
  // Lookup trusts the given hash and compares it with the stored one, so a
  // wrong hash misses (as upstream's tests/robin_map_tests.cpp:1396 checks).
  CHECK(map.count(key, hash ^ 1) == 0);
  CHECK(map.erase(key, hash) == 1 && map.count(key) == 0 && map.size() == 999);
}

static void move_only_value() {
  tsl::robin_map<int, std::unique_ptr<int>> map;
  map.emplace(1, std::make_unique<int>(10));
  map.try_emplace(2, std::make_unique<int>(20));
  // try_emplace leaves its argument alone when the key exists.
  auto keep = std::make_unique<int>(99);
  CHECK(!map.try_emplace(1, std::move(keep)).second && keep && *keep == 99);
  CHECK(*map.at(1) == 10 && *map.at(2) == 20);
  std::unique_ptr<int> taken = std::move(map.find(2).value());
  CHECK(taken && *taken == 20 && !map.at(2));
}

static void serialization_round_trip() {
  using Map = tsl::robin_map<std::int64_t, std::int64_t>;
  Map map;
  for (std::int64_t i = -500; i < 500; ++i) map.emplace(i, i * 3);
  Serializer out;
  map.serialize(out);
  CHECK(!out.bytes.empty());

  for (bool hash_compatible : {false, true}) {
    Deserializer in{out.bytes};
    const Map copy = Map::deserialize(in, hash_compatible);
    CHECK(in.at == out.bytes.size());
    CHECK(copy == map && copy.size() == 1000 && copy.at(-500) == -1500);
  }
}

int main() {
  basic_map();
  growth_and_erase_while_iterating();
  erase_shifts_back();
  prime_growth_policy();
  set_and_heterogeneous_lookup();
  precalculated_and_stored_hash();
  move_only_value();
  serialization_round_trip();
  if (failures != 0) return 1;
  std::puts("robin-map C++ consumer test passed");
  return 0;
}
