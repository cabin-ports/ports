#include <cista/serialization.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what.c_str());
    ++failures;
  }
}

// cista::offset and cista::raw are namespaces of the same containers with
// relative and absolute pointers; these aliases let one record use either.
struct Offset {
  using string = cista::offset::string;
  template <typename T>
  using vector = cista::offset::vector<T>;
  template <typename K, typename V>
  using hash_map = cista::offset::hash_map<K, V>;
};

struct Raw {
  using string = cista::raw::string;
  template <typename T>
  using vector = cista::raw::vector<T>;
  template <typename K, typename V>
  using hash_map = cista::raw::hash_map<K, V>;
};

template <typename Data>
struct Record {
  std::uint32_t id;
  typename Data::string name;
  typename Data::template vector<int> values;
  typename Data::template hash_map<typename Data::string, int> counts;
};

template <typename Data>
Record<Data> MakeRecord() {
  Record<Data> r{};
  r.id = 0x01020304U;
  r.name = "cista";
  r.values.push_back(1);
  r.values.push_back(2);
  r.values.push_back(3);
  r.counts["north"] = 7;
  r.counts["south"] = 11;
  return r;
}

template <typename Data>
bool Matches(const Record<Data>& r) {
  return r.id == 0x01020304U && r.name == std::string_view{"cista"} &&
         r.values.size() == 3 && r.values[0] == 1 && r.values[1] == 2 &&
         r.values[2] == 3 && r.counts.size() == 2 &&
         r.counts.at("north") == 7 && r.counts.at("south") == 11;
}

template <cista::mode Mode, typename Data>
void RoundTrip(const std::string& label) {
  auto original = MakeRecord<Data>();
  auto buf = cista::serialize<Mode>(original);
  auto* loaded = cista::deserialize<Record<Data>, Mode>(buf);
  check(loaded != nullptr && Matches(*loaded), label + ": round trip");
}

constexpr auto kChecked =
    cista::mode::WITH_VERSION | cista::mode::WITH_INTEGRITY;

std::string Error(cista::byte_buf& buf) {
  try {
    cista::deserialize<Record<Offset>, kChecked>(buf);
  } catch (const cista::cista_exception& e) {
    return e.what();
  }
  return "no exception";
}

}  // namespace

int main() {
  // FNV-1a 64 reference values from the algorithm's specification.
  check(cista::hash("") == 0xcbf29ce484222325ULL, "FNV-1a of \"\"");
  check(cista::hash("a") == 0xaf63dc4c8601ec8cULL, "FNV-1a of \"a\"");
  check(cista::hash("foobar") == 0x85944171f73967e8ULL,
        "FNV-1a of \"foobar\"");

  RoundTrip<cista::mode::NONE, Offset>("offset");
  RoundTrip<cista::mode::NONE, Raw>("raw");
  RoundTrip<kChecked, Offset>("offset, versioned and checksummed");

  // A big-endian buffer starts with the record's id, most significant byte
  // first, whatever the host's byte order.
  {
    constexpr auto kBig = cista::mode::SERIALIZE_BIG_ENDIAN;
    auto original = MakeRecord<Offset>();
    auto buf = cista::serialize<kBig>(original);
    const unsigned char id[] = {0x01, 0x02, 0x03, 0x04};
    check(buf.size() > 4 && std::memcmp(buf.data(), id, 4) == 0,
          "big-endian id bytes");
    auto* loaded = cista::deserialize<Record<Offset>, kBig>(buf);
    check(loaded != nullptr && Matches(*loaded), "big-endian round trip");
  }

  // The version (the type's hash) and the checksum take the first 16 bytes;
  // the checksum covers the data after them.
  {
    auto original = MakeRecord<Offset>();
    auto intact = cista::serialize<kChecked>(original);
    auto corrupted = intact;
    corrupted[16] ^= 0xFF;
    auto wrong_version = intact;
    wrong_version[0] ^= 0xFF;
    check(Error(intact) == "no exception", "intact buffer accepted");
    check(Error(corrupted) == "invalid checksum", "corrupted data rejected");
    check(Error(wrong_version) == "invalid version", "wrong version rejected");
  }

  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("cista consumer test passed");
  return 0;
}
