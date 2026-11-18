// The C++ consumer: tsl::ordered_map and tsl::ordered_set in C++11 - upstream's
// README example, then iteration in insertion order (and lookups landing on the
// right position) through growth, rehash, ordered erase by key, iterator,
// predicate and range (away from the limitation cabin.toml records), pop_back,
// insertion at a position and unordered_erase, which moves the last element
// into the hole; order-dependent equality, a std::vector-backed set's data(),
// and serialize/deserialize round trips.
#include <tsl/ordered_map.h>
#include <tsl/ordered_set.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
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

// Eight consecutive keys share a hash, and the groups' home buckets count down
// from the last bucket, so the entries pack into one run that wraps past the
// end of the table at every size: lookups probe, insertions displace, and
// erasures shift entries back.
struct ClusterHash {
  std::size_t operator()(int key) const {
    return ~static_cast<std::size_t>(key / 8);
  }
};

// Eight consecutive keys share a hash and the groups' home buckets count up
// from the first bucket: the entries pack into one run that stays clear of the
// last bucket, as erase(first, last) needs (see cabin.toml).
struct HomeHash {
  std::size_t operator()(int key) const {
    return static_cast<std::size_t>(key / 8);
  }
};

typedef tsl::ordered_map<int, int, ClusterHash> IntMap;
typedef std::pair<int, int> IntPair;

// The keys of a char-keyed map in iteration order.
static std::string charKeys(const tsl::ordered_map<char, int>& map) {
  std::string keys;
  for (tsl::ordered_map<char, int>::const_iterator it = map.begin();
       it != map.end(); ++it)
    keys += it->first;
  return keys;
}

// map iterates over exactly `order`, each key mapping to its negation, and
// find() returns each key at its position in that order.
template <class Map>
static bool matches(const Map& map, const std::vector<int>& order) {
  if (map.size() != order.size()) return false;
  std::size_t i = 0;
  for (typename Map::const_iterator it = map.begin(); it != map.end();
       ++it, ++i)
    if (it->first != order[i] || it->second != -order[i]) return false;
  for (i = 0; i < order.size(); ++i) {
    const typename Map::const_iterator it = map.find(order[i]);
    if (it == map.end() || it - map.begin() != static_cast<std::ptrdiff_t>(i))
      return false;
  }
  return true;
}

// Upstream's serializer contract: std::uint64_t, float and value_type.
struct Serializer {
  std::vector<unsigned char> bytes;
  template <class U>
  void raw(const U& value) {
    const unsigned char* p = reinterpret_cast<const unsigned char*>(&value);
    bytes.insert(bytes.end(), p, p + sizeof(U));
  }
  void write(std::uint64_t value) { raw(value); }
  void write(float value) { raw(value); }
  void write(const IntPair& value) {
    raw(value.first);
    raw(value.second);
  }
  template <class U>
  void operator()(const U& value) {
    write(value);
  }
};

struct Deserializer {
  const std::vector<unsigned char>* bytes;
  std::size_t at;
  template <class U>
  U raw() {
    U value;
    std::memcpy(&value, bytes->data() + at, sizeof(U));
    at += sizeof(U);
    return value;
  }
  std::uint64_t read(std::uint64_t*) { return raw<std::uint64_t>(); }
  float read(float*) { return raw<float>(); }
  IntPair read(IntPair*) {
    const int first = raw<int>();
    return IntPair(first, raw<int>());
  }
  template <class U>
  U operator()() {
    return read(static_cast<U*>(nullptr));
  }
};

static void readmeExample() {
  tsl::ordered_map<char, int> map = {{'d', 1}, {'a', 2}, {'g', 3}};
  map.insert({'b', 4});
  map['h'] = 5;
  map['e'] = 6;
  CHECK(charKeys(map) == "dagbhe");

  // An existing key keeps its place and its value.
  CHECK(!map.insert({'a', 9}).second && map.at('a') == 2);
  CHECK(!map.insert_or_assign('g', 30).second && map.at('g') == 30);
  map['g'] = 3;
  CHECK(charKeys(map) == "dagbhe");

  CHECK(map.erase('a') == 1);
  CHECK(charKeys(map) == "dgbhe");
  CHECK(map.at('d') == 1 && map.at('g') == 3 && map.at('b') == 4 &&
        map.at('h') == 5 && map.at('e') == 6);
  CHECK(map.nth(2)->first == 'b' && map.front().first == 'd' &&
        map.back().first == 'e');

  // The last element moves into the erased one's place.
  CHECK(map.unordered_erase('b') == 1);
  CHECK(charKeys(map) == "dgeh");

  for (tsl::ordered_map<char, int>::iterator it = map.begin(); it != map.end();
       ++it)
    it.value() += 2;
  CHECK(map.at('d') == 3 && map.at('g') == 5 && map.at('e') == 8 &&
        map.at('h') == 7);

  const std::size_t hash = std::hash<char>()('d');
  CHECK(map.find('d', hash) != map.end() && map.find('d', hash)->second == 3);
  CHECK(map.count('a') == 0 && map.find('b') == map.end());

  // Equality depends on the order.
  tsl::ordered_map<char, int> xy = {{'x', 1}, {'y', 2}};
  tsl::ordered_map<char, int> yx = {{'y', 2}, {'x', 1}};
  CHECK(xy != yx && !(xy == yx));
  yx.erase('y');
  yx.insert({'y', 2});
  CHECK(xy == yx);
}

static void checkRoundTrip(const IntMap& map, const std::vector<int>& order,
                           bool hashCompatible) {
  Serializer out;
  map.serialize(out);
  Deserializer in = {&out.bytes, 0};
  IntMap copy = IntMap::deserialize(in, hashCompatible);
  // Only the hash-compatible path reads the stored buckets back.
  if (hashCompatible) CHECK(in.at == out.bytes.size());
  CHECK(copy == map && matches(copy, order));
  CHECK(copy.insert(IntPair(-7, 7)).second && copy.back().first == -7 &&
        copy.find(-7) - copy.begin() == static_cast<std::ptrdiff_t>(order.size()));
}

// Inserts a permutation of 0..999, each key mapping to its negation, and
// returns the insertion order.
template <class Map>
static std::vector<int> fill(Map& map) {
  std::vector<int> order;
  for (int i = 0; i < 1000; ++i) {
    const int key = (i * 7919) % 1000;
    CHECK(map.insert(IntPair(key, -key)).second);
    order.push_back(key);
  }
  return order;
}

static void orderedMap() {
  IntMap map;
  const std::size_t initialBuckets = map.bucket_count();
  const std::vector<int> order = fill(map);
  CHECK(map.bucket_count() > initialBuckets);
  CHECK(matches(map, order));

  map.rehash(8192);
  CHECK(map.bucket_count() >= 8192 && matches(map, order));
  map.rehash(0);
  CHECK(map.bucket_count() < 8192 && matches(map, order));

  // Ordered erase by key keeps the remaining order.
  std::vector<int> rest;
  for (std::size_t i = 0; i < order.size(); ++i) {
    if (i % 3 == 0)
      CHECK(map.erase(order[i]) == 1);
    else
      rest.push_back(order[i]);
  }
  CHECK(map.erase(order[0]) == 0);
  CHECK(matches(map, rest));

  std::vector<int> odd;
  std::size_t evens = 0;
  for (std::size_t i = 0; i < rest.size(); ++i) {
    if (rest[i] % 2 == 0)
      ++evens;
    else
      odd.push_back(rest[i]);
  }
  CHECK(erase_if(map, [](const IntPair& kv) { return kv.first % 2 == 0; }) ==
        evens);
  CHECK(matches(map, odd));

  // Each erase is sequenced before the begin() it is compared with: erasing
  // invalidates the deque's iterators.
  for (int n = 0; n < 10; ++n) {
    const IntMap::iterator next = map.erase(map.begin() + 10);
    CHECK(next == map.begin() + 10);
  }
  odd.erase(odd.begin() + 10, odd.begin() + 20);
  CHECK(matches(map, odd));

  map.pop_back();
  odd.pop_back();
  CHECK(matches(map, odd));

  CHECK(map.insert_at_position(map.begin() + 1, IntPair(-5, 5)).second);
  odd.insert(odd.begin() + 1, -5);
  CHECK(matches(map, odd));
  // An existing key stays where it is.
  CHECK(!map.insert_at_position(map.begin(), IntPair(odd[3], 0)).second);
  CHECK(matches(map, odd));

  checkRoundTrip(map, odd, false);
  checkRoundTrip(map, odd, true);

  CHECK(map.unordered_erase(-5) == 1);
  odd[1] = odd.back();
  odd.pop_back();
  CHECK(matches(map, odd));
}

static void rangeErase() {
  tsl::ordered_map<int, int, HomeHash> map;
  std::vector<int> order = fill(map);
  CHECK(matches(map, order));

  tsl::ordered_map<int, int, HomeHash>::iterator next =
      map.erase(map.begin() + 10, map.begin() + 20);
  CHECK(next == map.begin() + 10);
  order.erase(order.begin() + 10, order.begin() + 20);
  CHECK(matches(map, order));
  next = map.erase(map.begin(), map.begin() + 5);
  CHECK(next == map.begin());
  order.erase(order.begin(), order.begin() + 5);
  CHECK(matches(map, order));
  next = map.erase(map.end() - 5, map.end());
  CHECK(next == map.end());
  order.erase(order.end() - 5, order.end());
  CHECK(matches(map, order));
}

static void orderedSet() {
  // Upstream's README: a std::vector-backed set hands its values to C APIs.
  typedef tsl::ordered_set<char, std::hash<char>, std::equal_to<char>,
                           std::allocator<char>, std::vector<char>>
      CharSet;
  CharSet chars;
  chars.reserve(6);
  chars = {'3', '4', '9', '2'};
  chars.erase('2');
  chars.insert('1');
  chars.insert('\0');
  chars.pop_back();
  chars.insert({'0', '\0'});
  CHECK(chars.size() == 6 && std::strcmp(chars.data(), "34910") == 0);
  const char expected[] = {'3', '4', '9', '1', '0', '\0'};
  CHECK(chars.values_container() ==
        std::vector<char>(expected, expected + sizeof expected));

  tsl::ordered_set<std::string> words;
  std::vector<std::string> order;
  for (int i = 0; i < 300; ++i) {
    const std::string word = "w" + std::to_string((i * 37) % 300);
    CHECK(words.insert(word).second);
    order.push_back(word);
  }
  CHECK(!words.insert(order[5]).second);
  words.rehash(4096);
  CHECK(std::vector<std::string>(words.begin(), words.end()) == order);
  CHECK(words.nth(5) != words.end() && *words.nth(5) == order[5]);
  CHECK(words.find(order[7]) - words.begin() == 7);

  CHECK(words.erase(order[0]) == 1);
  order.erase(order.begin());
  CHECK(words.unordered_erase(order[0]) == 1);
  order[0] = order.back();
  order.pop_back();
  CHECK(std::vector<std::string>(words.begin(), words.end()) == order);
  CHECK(words.count(order[0]) == 1 && words.find(order[0]) == words.begin());

  tsl::ordered_set<int> ab = {1, 2};
  tsl::ordered_set<int> ba = {2, 1};
  CHECK(ab != ba);
}

int main() {
  readmeExample();
  orderedMap();
  rangeErase();
  orderedSet();
  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::printf("tsl-ordered-map: ok\n");
  return 0;
}
