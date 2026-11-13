// Drives tsl::array_map (char and char32_t keys), tsl::array_set and
// tsl::array_pg_map through insertion, lookup, erase, iteration, rehash,
// growth and shrinking, and round-trips a map through the serialize and
// deserialize hooks, including into a map with another hash.
#include <tsl/array_map.h>
#include <tsl/array_set.h>

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char *what)
{
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

typedef tsl::array_map<char, int> Map;
typedef std::map<std::string, int> Expected;

// Iteration order is unspecified, so compare as an ordered map.
template <class M> Expected contents(const M &m)
{
    Expected out;
    for (typename M::const_iterator it = m.begin(); it != m.end(); ++it) {
        out[std::string(it.key(), it.key_size())] = it.value();
    }
    return out;
}

// operator== looks keys up only in its right-hand side, so a restored map's
// own lookups need this.
template <class M> bool finds_all(const M &m, const Expected &expected)
{
    for (Expected::const_iterator it = expected.begin(); it != expected.end(); ++it) {
        typename M::const_iterator found = m.find_ks(it->first.data(), it->first.size());
        if (found == m.end() || found.value() != it->second) {
            return false;
        }
    }
    return m.size() == expected.size();
}

void test_map()
{
    Map m;
    check(m.empty() && m.size() == 0 && m.bucket_count() == 0, "a new map has no buckets");

    check(m.insert("one", 1).second, "insert a new key");
    std::pair<Map::iterator, bool> again = m.insert("one", 9);
    check(!again.second && again.first.value() == 1, "insert keeps the existing value");
    m["two"] = 2;
    check(m.emplace("three", 3).second, "emplace a new key");
    check(!m.insert_or_assign("one", 11).second && m.at("one") == 11, "insert_or_assign overwrites");
    m.insert(std::string("four"), 4);
    check(m.emplace_ks("five-and-more", 4, 5).second, "emplace_ks takes a key prefix");
    check(m.insert_ks("a\0b", 3, 7).second, "insert_ks stores an embedded NUL");

    Expected expected;
    expected["one"] = 11;
    expected["two"] = 2;
    expected["three"] = 3;
    expected["four"] = 4;
    expected["five"] = 5;
    expected[std::string("a\0b", 3)] = 7;
    check(m.size() == 6 && contents(m) == expected, "iteration yields every key and value");

    check(m.at("two") == 2 && m.at_ks("a\0b", 3) == 7, "at finds keys");
    Map::iterator three = m.find("three");
    check(three != m.end() && three.value() == 3 && std::string(three.key()) == "three",
          "find returns the key and value");
    check(m.find("six") == m.end() && m.count("four") == 1 && m.count("fiv") == 0 && m.count("a") == 0,
          "lookups of absent keys");
    bool threw = false;
    try {
        m.at("missing");
    } catch (const std::out_of_range &) {
        threw = true;
    }
    check(threw, "at throws std::out_of_range for an absent key");

    // 16-bit key sizes, less the NUL terminator and the end-of-bucket marker.
    check(m.max_key_size() == 65533, "max_key_size with the default KeySizeT");
    threw = false;
    try {
        m.insert(std::string(m.max_key_size() + 1, 'x'), 0);
    } catch (const std::length_error &) {
        threw = true;
    }
    check(threw && m.size() == 6, "a key over max_key_size throws std::length_error and inserts nothing");

    *m.find("four") = 44;
    m.find("five").value() = 55;
    expected["four"] = 44;
    expected["five"] = 55;
    check(contents(m) == expected, "values are writable through iterators");

    Map copied(m);
    copied["one"] = 111;
    Map swapped;
    swapped.swap(copied);
    check(m.at("one") == 11 && swapped.at("one") == 111 && copied.empty() && swapped.size() == 6,
          "a copy is independent and swap exchanges contents");

    check(m.erase("two") == 1 && m.erase("two") == 0, "erase by key");
    m.erase(m.find("three"));
    expected.erase("two");
    expected.erase("three");
    check(m.size() == 4 && contents(m) == expected, "erase removes exactly those keys");

    // Power-of-two growth: a rehash rounds up, a reserve divides by the
    // default max load factor of 2.
    m.rehash(100);
    check(m.bucket_count() == 128 && contents(m) == expected, "rehash(100) gives 128 buckets");
    m.reserve(1000);
    check(m.bucket_count() == 512 && contents(m) == expected, "reserve(1000) gives 512 buckets");

    for (int i = 0; i < 2000; ++i) {
        const std::string key = "k" + std::to_string(i);
        m.insert(key, i);
        expected[key] = i;
    }
    check(m.size() == 2004 && m.bucket_count() == 1024 && m.load_factor() <= m.max_load_factor(),
          "growth doubles the buckets once the load factor would pass 2");
    check(contents(m) == expected, "every key survives growth");

    bool erased = true;
    for (int i = 0; i < 1500; ++i) {
        const std::string key = "k" + std::to_string(i);
        erased = erased && m.erase(key) == 1;
        expected.erase(key);
    }
    check(erased && m.size() == 504 && contents(m) == expected, "erase 1500 grown keys");
    m.shrink_to_fit();
    check(m.bucket_count() == 256 && contents(m) == expected, "shrink_to_fit rounds 504 / 2 up to 256 buckets");

    Map::const_iterator last = m.cbegin();
    for (int i = 0; i < 10; ++i, ++last) {
        expected.erase(std::string(last.key(), last.key_size()));
    }
    m.erase(m.cbegin(), last);
    check(m.size() == 494 && contents(m) == expected, "range erase removes exactly the first ten");

    m.clear();
    check(m.empty() && m.begin() == m.end(), "clear empties the map");
}

void test_lists_and_wide_keys()
{
    const Map listed = {{"a", 1}, {"b", 2}};
    check(listed.size() == 2 && listed.at("a") == 1 && listed.at("b") == 2, "initializer-list construction");

    tsl::array_map<char32_t, long long> wide;
    check(wide.insert(U"\u03ba\u03bb\u03b5\u03b9\u03b4\u03af", 1).second && wide.insert(U"key", 2).second,
          "char32_t keys insert");
    check(wide.at(U"key") == 2 && wide.count(U"ke") == 0 &&
              wide.find(U"\u03ba\u03bb\u03b5\u03b9\u03b4\u03af").value() == 1,
          "char32_t lookups");
    std::map<std::u32string, long long> seen;
    for (tsl::array_map<char32_t, long long>::const_iterator it = wide.cbegin(); it != wide.cend(); ++it) {
        seen[std::u32string(it.key(), it.key_size())] = it.value();
    }
    check(seen.size() == 2 && seen[U"key"] == 2 && seen[U"\u03ba\u03bb\u03b5\u03b9\u03b4\u03af"] == 1,
          "char32_t iteration");
    check(wide.erase(U"key") == 1 && wide.size() == 1, "char32_t erase");
}

void test_set()
{
    tsl::array_set<char> s;
    check(s.insert("alpha").second && !s.insert("alpha").second && s.insert("beta").second,
          "a set stores each key once");
    check(s.size() == 2 && s.count("alpha") == 1 && s.count("gamma") == 0, "set lookups");
    std::vector<std::string> keys;
    for (tsl::array_set<char>::const_iterator it = s.begin(); it != s.end(); ++it) {
        keys.push_back(it.key());
    }
    check(keys.size() == 2 && (keys[0] == "alpha" || keys[1] == "alpha") &&
              (keys[0] == "beta" || keys[1] == "beta"),
          "set iteration yields each key");
    check(s.erase("alpha") == 1 && s.size() == 1 && s.count("alpha") == 0, "set erase");
}

void test_prime_growth()
{
    tsl::array_pg_map<char, int> m;
    m.insert("x", 1);
    m.rehash(100);
    check(m.bucket_count() == 131, "prime growth rounds 100 up to the prime 131");
    check(m.at("x") == 1, "a prime-growth map keeps its keys across a rehash");
}

// Moves every key one bucket over from the default hash.
struct ShiftedHash {
    std::size_t operator()(const char *key, std::size_t size) const
    {
        return tsl::ah::str_hash<char>()(key, size) + 1;
    }
};

// Writes values byte for byte; the round trip stays on one machine.
struct Serializer {
    std::vector<char> bytes;

    template <class U> void operator()(const U &value)
    {
        const char *p = reinterpret_cast<const char *>(&value);
        bytes.insert(bytes.end(), p, p + sizeof(U));
    }

    void operator()(const char *value, std::size_t size) { bytes.insert(bytes.end(), value, value + size); }
};

struct Deserializer {
    const std::vector<char> *bytes;
    std::size_t position;

    template <class U> U operator()()
    {
        U value;
        std::memcpy(&value, bytes->data() + position, sizeof(U));
        position += sizeof(U);
        return value;
    }

    void operator()(char *out, std::size_t size)
    {
        std::memcpy(out, bytes->data() + position, size);
        position += size;
    }
};

void test_serialization()
{
    Map m;
    Expected expected;
    for (int i = 0; i < 100; ++i) {
        const std::string key = "key-" + std::to_string(i * 7);
        m.insert(key, -i);
        expected[key] = -i;
    }
    m.erase("key-0");
    expected.erase("key-0");

    Serializer out;
    m.serialize(out);
    check(!out.bytes.empty(), "serialize writes bytes");

    Deserializer in = {&out.bytes, 0};
    Map copy = Map::deserialize(in);
    check(in.position == out.bytes.size(), "deserialize reads every byte");
    check(copy == m && contents(copy) == expected && finds_all(copy, expected), "deserialize restores the map");

    Deserializer fast = {&out.bytes, 0};
    Map same = Map::deserialize(fast, true);
    check(same == m && contents(same) == expected && finds_all(same, expected) &&
              same.bucket_count() == m.bucket_count(),
          "a hash-compatible deserialize restores the map");

    // Only a re-bucketing deserialize can find keys under another hash.
    typedef tsl::array_map<char, int, ShiftedHash> ShiftedMap;
    Deserializer other = {&out.bytes, 0};
    ShiftedMap shifted = ShiftedMap::deserialize(other);
    check(other.position == out.bytes.size() && contents(shifted) == expected && finds_all(shifted, expected),
          "deserialize re-buckets keys for another hash");
}

} // namespace

int main()
{
    test_map();
    test_lists_and_wide_keys();
    test_set();
    test_prime_growth();
    test_serialization();
    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::puts("tsl-array-hash 0.7.2 ok");
    return 0;
}
