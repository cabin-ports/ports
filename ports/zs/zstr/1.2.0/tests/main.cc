#include <zstr.hpp>

#include <cstdio>
#include <sstream>
#include <string>

namespace {

int failures = 0;

void check(bool ok, const char *what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    failures++;
  }
}

}  // namespace

int main() {
  // Round trip: the default output is gzip.
  std::stringstream sink;
  {
    zstr::ostream zs(sink);
    zs << "hello, zstr\n" << 12345;
  }
  const std::string packed = sink.str();
  check(packed.size() >= 2 && static_cast<unsigned char>(packed[0]) == 0x1f &&
            static_cast<unsigned char>(packed[1]) == 0x8b,
        "gzip magic bytes");
  check(packed.find("hello") == std::string::npos, "output is compressed");

  std::stringstream src(packed);
  zstr::istream zi(src);
  std::string line;
  std::getline(zi, line);
  check(line == "hello, zstr", "round-trip line");
  int n = 0;
  zi >> n;
  check(n == 12345, "round-trip integer");

  // Plain input passes through unchanged.
  std::stringstream plain("plain text");
  zstr::istream zp(plain);
  std::getline(zp, line);
  check(line == "plain text", "plain text auto-detected");

  // gzip magic followed by garbage is a zlib error.
  bool threw = false;
  try {
    std::stringstream bad(std::string("\x1f\x8b\x08\x00garbage-garbage-garbage", 26));
    zstr::istream zb(bad);
    zb.exceptions(std::ios_base::badbit);
    std::string s;
    std::getline(zb, s);
  } catch (const zstr::Exception &) {
    threw = true;
  }
  check(threw, "corrupt gzip stream throws zstr::Exception");

  // Strict file streams throw on a failed open.
  threw = false;
  try {
    zstr::ifstream missing("/nonexistent-dir/zstr-missing.gz");
  } catch (const strict_fstream::Exception &) {
    threw = true;
  }
  check(threw, "missing file throws strict_fstream::Exception");

  return failures == 0 ? 0 : 1;
}
