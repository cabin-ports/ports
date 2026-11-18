// The C++ consumer: tsl::hopscotch_map/set and tsl::bhopscotch_map/set in
// C++17 - lookup, insertion and erasure (also while iterating), copies, a
// move-only value, heterogeneous and precalculated-hash lookups, the three
// growth policies' exact bucket counts, an exact hopscotch displacement in a
// small neighborhood, and a constant hash that fills one neighborhood and
// spills the rest into the overflow container: a list in insertion order for
// hopscotch_*, a std::map/std::set in key order for bhopscotch_*.
#include <tsl/bhopscotch_map.h>
#include <tsl/bhopscotch_set.h>
#include <tsl/hopscotch_map.h>
#include <tsl/hopscotch_set.h>

#include <cstddef>
#include <cstdio>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <ratio>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

static int failures = 0;

#define CHECK(cond)                                                    \
  do {                                                                 \
    if (!(cond)) {                                                     \
      std::fprintf(stderr, "main.cc:%d: check failed: %s\n", __LINE__, \
                   #cond);                                             \
      ++failures;                                                      \
    }                                                                  \
  } while (0)

// Every key lands in the same home bucket at every table size, so rehashing
// cannot help and the default neighborhood of 62 buckets fills up.
struct ZeroHash {
  std::size_t operator()(int) const { return 0; }
};

// Keys 0..99 share home bucket 0, 100..199 home bucket 1, and so on.
struct HundredHash {
  std::size_t operator()(int key) const {
    return static_cast<std::size_t>(key / 100);
  }
};

// MurmurHash3's 32-bit finalizer: consecutive keys land in pseudo-random
// buckets, so neighborhoods collide and entries get displaced.
struct ScatterHash {
  std::size_t operator()(int key) const {
    unsigned h = static_cast<unsigned>(key);
    h ^= h >> 16;
    h *= 0x85ebca6bu;
    h ^= h >> 13;
    h *= 0xc2b2ae35u;
    h ^= h >> 16;
    return h;
  }
};

// A hash and an equality that accept std::string and std::string_view alike.
struct TransparentHash {
  using is_transparent = void;
  std::size_t operator()(std::string_view s) const {
    return std::hash<std::string_view>()(s);
  }
};

template <class Map>
static std::vector<int> keysOf(const Map& map) {
  std::vector<int> keys;
  for (const auto& kv : map) keys.push_back(kv.first);
  return keys;
}

template <class Set>
static std::vector<int> elementsOf(const Set& set) {
  return std::vector<int>(set.begin(), set.end());
}

// Keys from `from` down to `to`.
static std::vector<int> descending(int from, int to) {
  std::vector<int> keys;
  for (int k = from; k >= to; --k) keys.push_back(k);
  return keys;
}

static std::vector<int> ascending(int from, int to) {
  std::vector<int> keys;
  for (int k = from; k <= to; ++k) keys.push_back(k);
  return keys;
}

static std::vector<int> concat(std::vector<int> a, const std::vector<int>& b) {
  a.insert(a.end(), b.begin(), b.end());
  return a;
}

static void basics() {
  tsl::hopscotch_map<std::string, int> map;
  map["a"] = 1;
  CHECK(map.insert({"b", 2}).second);
  CHECK(!map.insert({"b", 20}).second && map.at("b") == 2);
  CHECK(map.try_emplace("c", 3).second && !map.try_emplace("c", 30).second);
  CHECK(!map.insert_or_assign("c", 33).second && map.at("c") == 33);
  CHECK(map.size() == 3 && map.at("a") == 1 && map.find("b")->second == 2);
  CHECK(map.count("d") == 0 && !map.contains("d") && map.find("d") == map.end());

  for (auto it = map.begin(); it != map.end(); ++it) it.value() *= 10;
  CHECK(map.at("a") == 10 && map.at("b") == 20 && map.at("c") == 330);

  const std::size_t hash = std::hash<std::string>()("b");
  CHECK(map.find("b", hash) != map.end() && map.count("b", hash) == 1);

  CHECK(map.erase("a") == 1 && map.erase("a") == 0 && map.count("a") == 0);

  // Equality ignores the order of insertion.
  tsl::hopscotch_map<std::string, int> other{{"c", 330}, {"b", 20}};
  CHECK(map == other);
  other["b"] = 21;
  CHECK(map != other);

  // Erasing while iterating visits every element once.
  tsl::hopscotch_map<int, int> numbers;
  for (int i = 0; i < 1000; ++i) numbers.insert({i, i * i});
  CHECK(numbers.size() == 1000 && numbers.at(31) == 961);
  std::size_t visited = 0;
  for (auto it = numbers.begin(); it != numbers.end(); ++visited)
    it = (it->first % 2 == 0) ? numbers.erase(it) : std::next(it);
  CHECK(visited == 1000 && numbers.size() == 500);
  bool ok = true;
  for (int i = 0; i < 1000; ++i)
    ok = ok && numbers.count(i) == static_cast<std::size_t>(i % 2) &&
         (i % 2 == 0 || numbers.at(i) == i * i);
  CHECK(ok);

  const tsl::hopscotch_map<int, int> copy = numbers;
  CHECK(copy == numbers && copy.size() == 500);

  tsl::hopscotch_map<std::string, int, TransparentHash, std::equal_to<>> names;
  names.insert({"alpha", 1});
  names.insert({"beta", 2});
  const std::string_view beta = "beta";
  CHECK(names.find(beta) != names.end() && names.find(beta)->second == 2);
  CHECK(names.count(std::string_view("gamma")) == 0);
  CHECK(names.erase(beta) == 1 && names.size() == 1);

  tsl::hopscotch_map<int, std::unique_ptr<int>> owners;
  for (int i = 0; i < 200; ++i) owners.emplace(i, std::make_unique<int>(i * 3));
  ok = owners.size() == 200;
  for (int i = 0; i < 200; ++i) ok = ok && *owners.at(i) == i * 3;
  CHECK(ok);

  tsl::hopscotch_set<int> set{1, 2, 3};
  CHECK(set.count(2) == 1 && set.count(4) == 0);
  CHECK(set.insert(4).second && !set.insert(4).second && set.size() == 4);
  CHECK(set.erase(1) == 1 && !set.contains(1));
}

static void growthPolicies() {
  // Power of two (the default), prime (the _pg_ aliases) and mod.
  tsl::hopscotch_map<int, int> power;
  power.rehash(100);
  CHECK(power.bucket_count() == 128);
  tsl::hopscotch_pg_map<int, int> prime;
  prime.rehash(97);
  CHECK(prime.bucket_count() == 97);
  tsl::hopscotch_map<int, int, std::hash<int>, std::equal_to<int>,
                     std::allocator<std::pair<int, int>>, 62, false,
                     tsl::hh::mod_growth_policy<std::ratio<3, 2>>>
      mod;
  mod.rehash(100);
  CHECK(mod.bucket_count() == 100);

  // 88 elements pass the 0.9 load factor of 97 buckets: the next prime is 131.
  for (int i = 0; i < 100; ++i) prime.insert({i, -i});
  CHECK(prime.size() == 100 && prime.at(42) == -42 &&
        prime.bucket_count() == 131);
  for (int i = 0; i < 100; ++i) mod.insert({i, -i});
  // 90 elements reach the 0.9 load factor: 100 buckets grow by 3/2.
  CHECK(mod.size() == 100 && mod.bucket_count() == 150 && mod.at(99) == -99);

  tsl::bhopscotch_pg_map<int, int> bprime;
  bprime.rehash(100);
  CHECK(bprime.bucket_count() == 131);
}

template <class Hash>
using SmallMap = tsl::hopscotch_map<int, int, Hash, std::equal_to<int>,
                                    std::allocator<std::pair<int, int>>, 6, true>;

// A neighborhood of 6 buckets.  Keys 100..600 fill buckets 1..6, key 0 takes
// bucket 0, and key 1 (home 0) finds bucket 7 empty, outside its
// neighborhood: key 200 moves from bucket 2 to 7, and key 1 takes bucket 2.
// Keys 4000 and 2000 sit alone in buckets 40 and 20.
static void displacement() {
  SmallMap<HundredHash> map(64);
  for (int k : {100, 200, 300, 400, 500, 600, 0, 1, 4000, 2000})
    map.insert({k, -k});
  CHECK(map.bucket_count() == 64 && map.overflow_size() == 0);
  CHECK(keysOf(map) == std::vector<int>({0, 100, 1, 300, 400, 500, 600, 200,
                                         2000, 4000}));
  bool ok = true;
  for (const auto& kv : map) ok = ok && map.at(kv.first) == -kv.first;
  CHECK(ok);
}

// Many colliding neighborhoods, with stored hashes.
static void smallNeighborhood() {
  SmallMap<ScatterHash> map;
  for (int i = 0; i < 2000; ++i) map.insert({i, -i});
  CHECK(map.size() == 2000);
  bool ok = true;
  for (int i = 0; i < 2000; ++i) ok = ok && map.at(i) == -i;
  CHECK(ok);
  for (int i = 0; i < 2000; i += 3) map.erase(i);
  ok = map.size() == 2000 - 667;
  for (int i = 0; i < 2000; ++i)
    ok = ok && map.count(i) == (i % 3 == 0 ? 0u : 1u);
  CHECK(ok);
}

// 100 keys inserted from 99 down to 0 under ZeroHash: buckets 0..61 take the
// first 62 (99..38), the other 38 (37..0) overflow.  The table stops growing
// at 128 buckets: the 62 bucketed keys stay under its 0.9 load factor, and a
// rehash would not empty the full neighborhood.  Iteration visits the buckets
// in order, then the overflow container.
template <class Map>
static void checkOverflowMap(const std::vector<int>& overflowOrder) {
  Map map;
  for (int k = 99; k >= 0; --k) CHECK(map.insert({k, k * 2}).second);
  CHECK(map.size() == 100 && map.overflow_size() == 38 &&
        map.bucket_count() == 128);
  CHECK(keysOf(map) == concat(descending(99, 38), overflowOrder));
  bool ok = true;
  for (int k = 0; k < 100; ++k) ok = ok && map.at(k) == k * 2;
  CHECK(ok);
  CHECK(!map.insert({5, 0}).second && map.at(5) == 10);
  // Overflow elements through iterators: find, value(), and erase (below).
  CHECK(map.find(5) != map.end() && map.find(5)->second == 10);
  map.find(5).value() = 11;
  CHECK(map.at(5) == 11);
  map.find(5).value() = 10;

  // Rehashing leaves the overflow where it is.
  map.rehash(map.bucket_count() * 4);
  CHECK(map.overflow_size() == 38);
  CHECK(keysOf(map) == concat(descending(99, 38), overflowOrder));

  // Erase one key from the buckets and, by iterator, one from the overflow,
  // which returns the overflow element after it; a new key takes the freed
  // bucket.
  CHECK(map.erase(50) == 1);
  int afterTen = -1;
  for (std::size_t i = 0; i + 1 < overflowOrder.size(); ++i)
    if (overflowOrder[i] == 10) afterTen = overflowOrder[i + 1];
  const auto next = map.erase(map.find(10));
  CHECK(next != map.end() && next->first == afterTen);
  CHECK(map.erase(10) == 0);
  CHECK(map.size() == 98 && map.overflow_size() == 37);
  CHECK(map.insert({100, 200}).second && map.overflow_size() == 37);
  std::vector<int> buckets = descending(99, 38);
  buckets[99 - 50] = 100;
  std::vector<int> overflow;
  for (int k : overflowOrder)
    if (k != 10) overflow.push_back(k);
  CHECK(keysOf(map) == concat(buckets, overflow));
  CHECK(map.count(50) == 0 && map.count(10) == 0 && map.at(100) == 200);
  ok = true;
  for (int k = 0; k < 100; ++k)
    ok = ok && (k == 50 || k == 10 || map.at(k) == k * 2);
  CHECK(ok);

  map.clear();
  CHECK(map.empty() && map.overflow_size() == 0);
}

template <class Set>
static void checkOverflowSet(const std::vector<int>& overflowOrder) {
  Set set;
  for (int k = 99; k >= 0; --k) set.insert(k);
  CHECK(set.size() == 100 && set.overflow_size() == 38 &&
        set.bucket_count() == 128);
  CHECK(elementsOf(set) == concat(descending(99, 38), overflowOrder));
  CHECK(set.erase(0) == 1 && set.count(0) == 0 && set.overflow_size() == 37);
}

static void overflow() {
  // hopscotch_*: the overflow is a std::list, so it keeps insertion order.
  checkOverflowMap<tsl::hopscotch_map<int, int, ZeroHash>>(descending(37, 0));
  checkOverflowSet<tsl::hopscotch_set<int, ZeroHash>>(descending(37, 0));
  // bhopscotch_*: the overflow is a std::map/std::set, ordered by key.
  checkOverflowMap<tsl::bhopscotch_map<int, int, ZeroHash>>(ascending(0, 37));
  checkOverflowSet<tsl::bhopscotch_set<int, ZeroHash>>(ascending(0, 37));
}

int main() {
  basics();
  growthPolicies();
  displacement();
  smallNeighborhood();
  overflow();
  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::printf("tsl-hopscotch-map: ok\n");
  return 0;
}
