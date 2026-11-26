#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>
#include <wirestead/wirestead.hpp>

namespace {

int failures = 0;

void check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

using wirestead::memory::ConstByteSpan;

std::vector<std::string> feed_collect(wirestead::framer::IFramer& f, const std::vector<std::string>& chunks) {
  std::vector<std::string> out;
  f.on_message([&](ConstByteSpan s) { out.emplace_back(s.begin(), s.end()); });
  for (const auto& c : chunks) {
    f.push_bytes(ConstByteSpan(reinterpret_cast<const uint8_t*>(c.data()), c.size()));
  }
  return out;
}

void test_framers() {
  using wirestead::framer::LengthPrefixFramer;
  using wirestead::framer::LineFramer;

  LineFramer line("\n");
  auto lines = feed_collect(line, {"abc\ndef\nxy", "z\n"});
  check((lines == std::vector<std::string>{"abc", "def", "xyz"}), "LineFramer splits across chunks");

  LineFramer crlf("\r\n", true);
  auto kept = feed_collect(crlf, {"ok\r\n"});
  check((kept == std::vector<std::string>{"ok\r\n"}), "LineFramer keeps the delimiter on request");

  LengthPrefixFramer big(2, LengthPrefixFramer::Endian::Big);
  auto frames = feed_collect(big, {std::string("\x00\x03" "abc\x00\x01", 7), "q"});
  check((frames == std::vector<std::string>{"abc", "q"}), "LengthPrefixFramer big-endian frames");

  LengthPrefixFramer little(2, LengthPrefixFramer::Endian::Little);
  auto lf = feed_collect(little, {std::string("\x02\x00xy", 4)});
  check((lf == std::vector<std::string>{"xy"}), "LengthPrefixFramer little-endian frames");

  bool threw = false;
  try {
    LengthPrefixFramer bad(3);
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  check(threw, "LengthPrefixFramer rejects a 3-byte prefix");
}

}  // namespace

int main() {
  test_framers();
  return failures == 0 ? 0 : 1;
}
