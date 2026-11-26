// Drives tsl::htrie_map and tsl::htrie_set through insertion, lookup, erase,
// prefix search, longest prefix match and the serialize and deserialize hooks.
#include <tsl/htrie_map.h>
#include <tsl/htrie_set.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <set>
#include <string>
#include <type_traits>

namespace {

int failures = 0;

void check(bool ok, const char *what)
{
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

typedef tsl::htrie_map<char, int> Map;
typedef std::set<std::string> Keys;

// Iteration order is unspecified, so compare keys as a set.
template <class It> Keys keys_of(It first, It last)
{
    Keys out;
    std::string buffer;
    for (; first != last; ++first) {
        first.key(buffer);
        out.insert(buffer);
    }
    return out;
}

Keys keys(const char *a, const char *b = nullptr, const char *c = nullptr, const char *d = nullptr,
          const char *e = nullptr)
{
    Keys out;
    for (const char *k : {a, b, c, d, e}) {
        if (k) {
            out.insert(k);
        }
    }
    return out;
}

void test_map()
{
    Map map = {{"one", 1}, {"two", 2}};
    map["three"] = 3;
    map["four"] = 4;
    check(map.insert("five", 5).second, "insert a new key");
    check(!map.insert("five", 50).second && map.at("five") == 5, "insert keeps an existing value");
    check(map.insert_ks("six_with_extra_chars_we_ignore", 3, 6).second, "insert_ks uses the given length");
    check(map.size() == 6, "size after inserts");

    check(map.at("six") == 6, "insert_ks stored the truncated key");
    check(map.count("six_with_extra_chars_we_ignore") == 0, "the full key was not stored");
    check(map.find("three") != map.end() && map.find("three").value() == 3, "find returns the value");
    check(map.find("absent") == map.end(), "find misses an absent key");

    check(map.erase("two") == 1, "erase removes one key");
    check(map.erase("two") == 0, "erase of an absent key removes none");
    check(map.size() == 5 && map.count("two") == 0, "size after erase");
    check(keys_of(map.begin(), map.end()) == keys("five", "four", "one", "six", "three"), "remaining keys");
}

void test_prefix()
{
    Map map = {{"apple", 1}, {"mango", 2}, {"apricot", 3}, {"mandarin", 4}, {"melon", 5}, {"macadamia", 6}};

    auto range = map.equal_prefix_range("ma");
    check(keys_of(range.first, range.second) == keys("macadamia", "mandarin", "mango"), "equal_prefix_range keys");
    int sum = 0;
    for (auto it = range.first; it != range.second; ++it) {
        sum += it.value();
    }
    check(sum == 6 + 4 + 2, "equal_prefix_range values");
    check(std::distance(map.equal_prefix_range("zzz").first, map.equal_prefix_range("zzz").second) == 0,
          "a prefix nothing matches yields an empty range");

    auto lp = map.longest_prefix("apple juice");
    check(lp != map.end() && lp.key() == "apple" && lp.value() == 1, "longest_prefix finds a stored key");
    check(map.longest_prefix("banana") == map.end(), "longest_prefix with no match is end");

    check(map.erase_prefix("ma") == 3, "erase_prefix count");
    check(map.size() == 3 && keys_of(map.begin(), map.end()) == keys("apple", "apricot", "melon"),
          "erase_prefix leaves the other keys");

    // 0.7.1 fixed erase_prefix when it empties a whole hash node: 16 keys fill
    // a burst_threshold of 16.
    tsl::htrie_map<char, std::int64_t> full(16);
    for (int i = 0; i < 16; i++) {
        full.insert("Key " + std::to_string(i), i);
    }
    check(full.size() == 16, "sixteen keys inserted");
    check(full.erase_prefix("Ke") == 16, "erase_prefix empties a hash node");
    check(full.size() == 0 && full.equal_prefix_range("Ke").first == full.equal_prefix_range("Ke").second,
          "no key remains after emptying a hash node");
}

void test_set()
{
    tsl::htrie_set<char> set = {"one", "two", "three"};
    set.insert({"four", "five"});
    check(set.size() == 5 && set.count("four") == 1 && set.count("six") == 0, "htrie_set insert and count");
    check(set.erase("one") == 1 && set.size() == 4 && set.count("one") == 0, "htrie_set erase");
    auto range = set.equal_prefix_range("t");
    check(keys_of(range.first, range.second) == keys("three", "two"), "htrie_set equal_prefix_range");
    auto lp = set.longest_prefix("fiver");
    check(lp != set.end() && lp.key() == "five", "htrie_set longest_prefix");
}

// The serializer and deserializer of README.md ("Serialization"), over a string.
struct Serializer {
    std::string out;
    template <class T, typename std::enable_if<std::is_arithmetic<T>::value>::type * = nullptr>
    void operator()(const T &value)
    {
        out.append(reinterpret_cast<const char *>(&value), sizeof(T));
    }
    void operator()(const char *value, std::size_t size) { out.append(value, size); }
};

struct Deserializer {
    const std::string &in;
    std::size_t pos;
    explicit Deserializer(const std::string &data) : in(data), pos(0) {}
    template <class T, typename std::enable_if<std::is_arithmetic<T>::value>::type * = nullptr> T operator()()
    {
        T value;
        std::memcpy(&value, in.data() + pos, sizeof(T));
        pos += sizeof(T);
        return value;
    }
    void operator()(char *out, std::size_t size)
    {
        std::memcpy(out, in.data() + pos, size);
        pos += size;
    }
};

void test_serialize()
{
    const Map map = {{"one", 1}, {"two", 2}, {"three", 3}, {"four", 4}};
    Serializer serial;
    map.serialize(serial);
    check(!serial.out.empty(), "serialize wrote data");

    Deserializer plain(serial.out);
    Map restored = Map::deserialize(plain);
    check(restored == map && restored.size() == 4 && restored.at("three") == 3, "round trip");

    Deserializer fast(serial.out);
    Map compat = Map::deserialize(fast, true);
    check(compat == map && compat.at("four") == 4, "round trip with hash_compatible");
}

} // namespace

int main()
{
    test_map();
    test_prefix();
    test_set();
    test_serialize();
    if (failures) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::puts("ok");
    return 0;
}
