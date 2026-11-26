#include <ulid.hh>

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {

int failures = 0;

void expect(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "check failed: %s\n", what);
    ++failures;
  }
}

}  // namespace

// Values are upstream's own: README.md:27 and src/ulid_test.cc:122-163.
int main() {
  const char* const text = "0001C7STHC0G2081040G208104";
  ulid::ULID u = ulid::Create(1484581420, []() { return 4; });

  expect(ulid::Marshal(u) == text, "Marshal");
  expect(ulid::Time(u) == 1484581420, "Time");

  expect(ulid::CompareULIDs(u, ulid::Unmarshal(text)) == 0, "Unmarshal");

  std::vector<std::uint8_t> bytes = ulid::MarshalBinary(u);
  expect(bytes.size() == 16, "MarshalBinary size");
  expect(bytes[2] == 0x58 && bytes[5] == 0x2C && bytes[6] == 4 && bytes[15] == 4, "MarshalBinary layout");
  expect(ulid::CompareULIDs(u, ulid::UnmarshalBinary(bytes)) == 0, "UnmarshalBinary");

  ulid::ULID t = 0;
  ulid::EncodeTime(std::uint64_t(1469918176385), t);
  expect(ulid::Marshal(t) == "01ARYZ6S410000000000000000", "EncodeTime");

  expect(ulid::CompareULIDs(ulid::Create(1484581420, []() { return 3; }), u) == -1, "order lt");
  expect(ulid::CompareULIDs(u, ulid::Create(1484581420, []() { return 3; })) == 1, "order gt");

  return failures == 0 ? 0 : 1;
}
