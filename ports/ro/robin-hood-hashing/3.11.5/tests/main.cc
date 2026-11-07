// Consumer test for cabin-ports/robin-hood-hashing: the flat and node
// maps through many rehashes, erase and iteration, copies, the
// try_emplace and insert_or_assign members it offers from C++11, a set,
// the node variants' reference stability, and its hash functions' exact
// values.
#include <robin_hood.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>

namespace {

int failures = 0;

void check(bool ok, const char* what) {
  if (!ok) {
    std::printf("FAILED: %s\n", what);
    ++failures;
  }
}

}  // namespace

int main() {
  robin_hood::unordered_flat_map<int, long long> squares;
  for (int i = 0; i < 100000; ++i)
    squares[i] = static_cast<long long>(i) * i;
  check(squares.size() == 100000, "the flat map holds every key");
  check(squares.count(316) == 1 && squares.at(316) == 99856, "flat map lookup");
  check(squares.find(100000) == squares.end(), "a missing key is not found");

  std::size_t erased = 0;
  for (int i = 0; i < 100000; i += 2)
    erased += squares.erase(i);
  check(erased == 50000 && squares.size() == 50000, "erase removes each even key once");
  long long sum = 0;
  for (auto const& kv : squares)
    sum += kv.first;
  check(sum == 2500000000LL, "iteration visits every odd key once");
  bool threw = false;
  try {
    squares.at(0);
  } catch (std::out_of_range const&) {
    threw = true;
  }
  check(threw, "at() throws for an erased key");

  robin_hood::unordered_flat_map<int, long long> copy = squares;
  check(copy == squares, "a copy compares equal");
  copy[1] = 0;
  check(!(copy == squares), "a changed value compares unequal");

  robin_hood::unordered_map<std::string, int> words;
  const std::string robin = "robin";
  bool first = words.try_emplace(robin, 1).second;
  bool second = words.try_emplace(robin, 2).second;
  check(first && !second && words[robin] == 1, "try_emplace keeps an existing value");
  bool inserted = words.insert_or_assign(robin, 3).second;
  check(!inserted && words[robin] == 3, "insert_or_assign overwrites");

  robin_hood::unordered_node_map<int, std::string> nodes;
  nodes[7] = "seven";
  std::string* seven = &nodes[7];
  for (int i = 100; i < 10100; ++i)
    nodes[i] = std::to_string(i);
  check(seven == &nodes[7] && *seven == "seven",
        "a node map keeps references across rehashes");
  robin_hood::unordered_node_map<int, std::string> nodes2 = nodes;
  check(nodes2 == nodes && &nodes2[7] != seven && nodes2[7] == "seven",
        "a node map copies deeply");

  robin_hood::unordered_set<std::string> set;
  bool added = set.insert("hood").second;
  bool again = set.insert("hood").second;
  check(added && !again && set.size() == 1, "a set rejects a duplicate");

  // A pointer hashes as its address; with GCC or Clang on a 64-bit target,
  // a misdetected 32-bit size type would not compile here.
  int x = 0;
  check(robin_hood::hash<int*>{}(&x) ==
            robin_hood::hash_int(reinterpret_cast<std::uintptr_t>(&x)),
        "a pointer hashes as its address");

  // MurmurHash64A over the bytes, and MurmurHash3's finalizer for
  // integers, both without their last mixing step (64-bit size_t only).
  if (sizeof(std::size_t) == 8) {
    check(robin_hood::hash<std::string>{}("robin hood") ==
              static_cast<std::size_t>(UINT64_C(0x5cc937e1d47d4dbd)),
          "string hash value");
    check(robin_hood::hash_bytes("", 0) == static_cast<std::size_t>(UINT64_C(0xe17a1465)),
          "empty input hashes to the seed");
    check(robin_hood::hash<int>{}(42) ==
              static_cast<std::size_t>(UINT64_C(0xe366d96c81ba7514)),
          "integer hash value");
    check(robin_hood::hash<int>{}(-1) ==
              static_cast<std::size_t>(UINT64_C(0x0955399984aa9ccc)),
          "a negative integer hashes as its 64-bit pattern");
  }

  std::printf("robin-hood-hashing (C++): %s\n", failures == 0 ? "ok" : "FAILED");
  return failures == 0 ? 0 : 1;
}
