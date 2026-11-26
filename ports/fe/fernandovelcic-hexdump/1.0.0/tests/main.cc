// Dumps known bytes through each public class and compares the exact text.
#include <hexdump.hpp>

#include <cstdio>
#include <iomanip>
#include <sstream>
#include <string>

namespace {

bool ok = true;

void check(const std::string& got, const std::string& want, const char* what) {
  if (got != want) {
    std::fprintf(stderr, "FAIL: %s\n got: [%s]\nwant: [%s]\n", what,
                 got.c_str(), want.c_str());
    ok = false;
  }
}

std::string spaces(std::size_t n) { return std::string(n, ' '); }

}  // namespace

int main() {
  unsigned char d[] = {0x48, 0x69, 0x21, 0x01, 0x7f, 0x80};

  // Default 16-byte rows; 10 missing bytes are padded, then ASCII with '.'
  // for non-printable bytes.
  std::ostringstream a;
  a << Hexdump(d, 6);
  check(a.str(), "0x000000: 48 69 21 01 7f 80 " + spaces(31) + "Hi!...\n",
        "Hexdump single row");

  // Rows of 4 without the ASCII column; the last row is padded.
  std::ostringstream b;
  b << CustomHexdump<4, false>(d, 6);
  check(b.str(), "0x000000: 48 69 21 01  \n0x000004: 7f 80 " + spaces(7) + "\n",
        "CustomHexdump<4, false>");

  // Two rows of 8 over 10 bytes with the ASCII column and a row offset.
  unsigned char t[] = {'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J'};
  std::ostringstream c;
  c << CustomHexdump<8, true>(t, 10);
  check(c.str(),
        "0x000000: 41 42 43 44 45 46 47 48  ABCDEFGH\n"
        "0x000008: 49 4a " + spaces(18) + " IJ\n",
        "CustomHexdump<8, true> two rows");

  // Custom length type; only the first two bytes are dumped.
  std::ostringstream e;
  e << CustomHexdumpBase<unsigned short, 8, true>(d, 2);
  check(e.str(), "0x000000: 48 69 " + spaces(19) + "Hi\n",
        "CustomHexdumpBase<unsigned short>");

  // Empty input prints nothing.
  std::ostringstream z;
  z << Hexdump(d, 0);
  check(z.str(), "", "empty dump");

  // Accessors.
  Hexdump h(d, 6);
  if (h.getLength() != 6 || h.getData(4) != 0x7f) {
    std::fprintf(stderr, "FAIL: accessors\n");
    ok = false;
  }

  // operator<< leaves the stream in hex with fill '0'.
  std::ostringstream s;
  s << Hexdump(d, 1) << std::setw(5) << 255;
  check(s.str(), "0x000000: 48 " + spaces(46) + "H\n000ff", "stream state");

  return ok ? 0 : 1;
}
