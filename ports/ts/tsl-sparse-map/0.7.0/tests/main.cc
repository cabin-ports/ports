// Drives tsl::sparse_map, tsl::sparse_set and tsl::sparse_pg_map through
// insertion, lookup, erase, iteration, rehash, growth and probing past erased
// buckets, and round-trips maps through the serialize/deserialize hooks.
#include <tsl/sparse_map.h>
#include <tsl/sparse_set.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
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

typedef tsl::sparse_map<int, int> Map;
typedef std::map<int, int> Expected;

// Iteration order is unspecified, so compare as an ordered map.
template <class M> Expected contents(const M &m)
{
    Expected out;
    for (typename M::const_iterator it = m.begin(); it != m.end(); ++it) {
        out[it->first] = it->second;
    }
    return out;
}

// operator== looks keys up only in its right-hand side, so a restored map's
// own lookups need this.
template <class M> bool finds_all(const M &m, const Expected &expected)
{
    for (Expected::const_iterator it = expected.begin(); it != expected.end(); ++it) {
        typename M::const_iterator found = m.find(it->first);
        if (found == m.end() || found->second != it->second) {
            return false;
        }
    }
    return m.size() == expected.size();
}

// Deterministic bucket placement on every standard library.
struct IdentityHash {
    std::size_t operator()(int key) const { return static_cast<std::size_t>(key); }
};

struct ZeroHash {
    std::size_t operator()(int) const { return 0; }
};

void test_map()
{
    Map m;
    check(m.empty() && m.bucket_count() == 0, "a new map has no buckets");

    check(m.insert(std::make_pair(1, 10)).second, "insert a new key");
    std::pair<Map::iterator, bool> again = m.insert(std::make_pair(1, 99));
    check(!again.second && again.first->second == 10, "insert keeps the existing value");
    m[2] = 20;
    check(m.emplace(3, 30).second && m.try_emplace(4, 40).second && !m.try_emplace(4, 44).second,
          "emplace and try_emplace");
    check(m.at(4) == 40, "try_emplace keeps the existing value");
    check(!m.insert_or_assign(1, 11).second && m.at(1) == 11, "insert_or_assign overwrites");
    m.find(4).value() = 41;

    Expected expected;
    expected[1] = 11;
    expected[2] = 20;
    expected[3] = 30;
    expected[4] = 41;
    check(m.size() == 4 && contents(m) == expected, "iteration yields every key and value");

    check(m.at(2) == 20 && m.find(3) != m.end() && m.find(3)->second == 30, "at and find");
    check(m.count(5) == 0 && !m.contains(5) && m.contains(4) && m.find(5) == m.end(), "absent keys");
    std::pair<Map::iterator, Map::iterator> range = m.equal_range(3);
    check(range.first != range.second && range.first->first == 3 && ++range.first == range.second,
          "equal_range spans one element");
    bool threw = false;
    try {
        m.at(5);
    } catch (const std::out_of_range &) {
        threw = true;
    }
    check(threw, "at throws std::out_of_range for an absent key");

    check(m.erase(2) == 1 && m.erase(2) == 0, "erase by key");
    m.erase(m.find(3));
    expected.erase(2);
    expected.erase(3);
    check(m.size() == 2 && contents(m) == expected, "erase removes exactly those keys");

    // Power-of-two growth with the default max load factor of 0.5: a rehash
    // rounds up, a reserve doubles before rounding.
    m.rehash(100);
    check(m.bucket_count() == 128 && contents(m) == expected, "rehash(100) gives 128 buckets");
    m.reserve(1000);
    check(m.bucket_count() == 2048 && contents(m) == expected, "reserve(1000) gives 2048 buckets");

    for (int i = 0; i < 2000; ++i) {
        m.insert(std::make_pair(100000 + i, i));
        expected[100000 + i] = i;
    }
    check(m.size() == 2002 && m.bucket_count() == 4096 && m.load_factor() <= m.max_load_factor(),
          "growth doubles the buckets once the load factor would pass 0.5");
    check(contents(m) == expected && finds_all(m, expected), "every key survives growth");

    Map copied(m);
    copied[100000] = -1;
    check(m.at(100000) == 0 && copied.at(100000) == -1, "a copy is independent");
    copied[100000] = 0;
    Map moved(std::move(copied));
    check(moved == m && finds_all(moved, expected), "a moved-to map keeps every key");

    for (int i = 0; i < 2000; i += 2) {
        m.erase(100000 + i);
        expected.erase(100000 + i);
    }
    check(m.size() == 1002 && contents(m) == expected && finds_all(m, expected) && m.count(100000) == 0,
          "erasing every other key");

    m.clear();
    check(m.empty() && m.begin() == m.end(), "clear empties the map");
}

void test_probing()
{
    // One home bucket for every key: lookups and inserts walk the probe chain
    // through erased buckets and reuse them.
    tsl::sparse_map<int, int, ZeroHash> chain;
    for (int i = 0; i < 100; ++i) {
        chain[i] = i * 2;
    }
    bool ok = true;
    for (int i = 0; i < 100; i += 2) {
        ok = ok && chain.erase(i) == 1;
    }
    for (int i = 0; i < 100; ++i) {
        ok = ok && chain.count(i) == (i % 2 == 0 ? 0u : 1u) && (i % 2 == 0 || chain.at(i) == i * 2);
    }
    check(ok && chain.size() == 50, "lookups probe past erased buckets");
    for (int i = 0; i < 100; i += 2) {
        chain[i] = -i;
    }
    ok = chain.size() == 100;
    for (int i = 0; i < 100; ++i) {
        ok = ok && chain.at(i) == (i % 2 == 0 ? -i : i * 2);
    }
    check(ok, "inserts reuse erased buckets");

    // 64 buckets: 31 keys inserted and erased leave 31 deleted buckets, and
    // the insert that finds size plus deleted at 48 rebuilds the table at the
    // same bucket count.
    tsl::sparse_map<int, int, IdentityHash> purge;
    purge.rehash(64);
    for (int i = 0; i <= 30; ++i) {
        purge[i] = i;
    }
    for (int i = 0; i <= 30; ++i) {
        purge.erase(i);
    }
    for (int i = 31; i <= 48; ++i) {
        purge[i] = i;
    }
    ok = purge.size() == 18 && purge.bucket_count() == 64;
    for (int i = 0; i <= 48; ++i) {
        ok = ok && purge.count(i) == (i <= 30 ? 0u : 1u);
    }
    check(ok, "clearing deleted buckets keeps the live keys and the bucket count");
}

void test_set()
{
    tsl::sparse_set<std::string> s;
    check(s.insert("alpha").second && !s.insert("alpha").second && s.emplace("beta").second,
          "a set stores each key once");
    check(s.size() == 2 && s.count("alpha") == 1 && s.count("gamma") == 0, "set lookups");
    std::set<std::string> keys(s.begin(), s.end());
    std::set<std::string> want;
    want.insert("alpha");
    want.insert("beta");
    check(keys == want, "set iteration yields each key");
    check(s.erase("alpha") == 1 && s.size() == 1 && s.count("alpha") == 0, "set erase");
}

void test_prime_growth()
{
    tsl::sparse_pg_map<int, int> m;
    m[7] = 70;
    m.rehash(100);
    check(m.bucket_count() == 131, "prime growth rounds 100 up to the prime 131");
    check(m.at(7) == 70, "a prime-growth map keeps its keys across a rehash");
}

// Writes values byte for byte; the round trip stays on one machine.
struct Serializer {
    std::vector<char> bytes;

    template <class U> void operator()(const U &value)
    {
        const char *p = reinterpret_cast<const char *>(&value);
        bytes.insert(bytes.end(), p, p + sizeof(U));
    }

    void operator()(const std::pair<int, int> &value)
    {
        (*this)(value.first);
        (*this)(value.second);
    }
};

struct Deserializer {
    const std::vector<char> *bytes;
    std::size_t position;

    template <class U> U operator()() { return read(static_cast<U *>(nullptr)); }

    template <class U> U read(U *)
    {
        U value;
        std::memcpy(&value, bytes->data() + position, sizeof(U));
        position += sizeof(U);
        return value;
    }

    std::pair<int, int> read(std::pair<int, int> *)
    {
        const int first = read(static_cast<int *>(nullptr));
        const int second = read(static_cast<int *>(nullptr));
        return std::make_pair(first, second);
    }
};

void test_serialization()
{
    Map m;
    for (int i = 0; i < 300; ++i) {
        m[i * 3] = -i;
    }
    m.erase(0);
    m.erase(3);
    m.reserve(1000);
    const Expected expected = contents(m);

    Serializer out;
    m.serialize(out);
    check(!out.bytes.empty(), "serialize writes bytes");

    Deserializer in = {&out.bytes, 0};
    Map copy = Map::deserialize(in);
    check(in.position == out.bytes.size(), "deserialize reads every byte");
    check(copy == m && finds_all(copy, expected) && copy.bucket_count() == 1024,
          "deserialize restores the map and sizes it for its elements");

    Deserializer fast = {&out.bytes, 0};
    Map same = Map::deserialize(fast, true);
    check(fast.position == out.bytes.size() && same == m && finds_all(same, expected) && m.bucket_count() == 2048 &&
              same.bucket_count() == 2048,
          "a hash-compatible deserialize keeps the buckets");

    // Key 191 probes from its erased home bucket 63 into 64.  Upstream's
    // hash-compatible deserialize drops the erased mark of a bucket group
    // with no values and so loses 191; the default re-inserts every key.
    typedef tsl::sparse_map<int, int, IdentityHash> IdentityMap;
    IdentityMap probed;
    probed.rehash(128);
    probed[63] = 1;
    probed[191] = 2;
    probed.erase(63);
    Serializer probed_out;
    probed.serialize(probed_out);
    Deserializer probed_in = {&probed_out.bytes, 0};
    IdentityMap rebuilt = IdentityMap::deserialize(probed_in);
    check(rebuilt.size() == 1 && rebuilt.count(63) == 0 && rebuilt.at(191) == 2, "deserialize after an erase");
}

} // namespace

int main()
{
    test_map();
    test_probing();
    test_set();
    test_prime_growth();
    test_serialization();
    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::puts("tsl-sparse-map 0.7.0 ok");
    return 0;
}
