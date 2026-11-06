// C++98: the probe compiles at the interface minimum, so only the C++98
// API is used here.
#include <utf8.h>

#include <cstdio>
#include <iterator>
#include <string>
#include <vector>

namespace {

int fail(const char *what) {
  std::fprintf(stderr, "utfcpp: %s\n", what);
  return 1;
}

}  // namespace

int main() {
  // "a", U+00F1, U+20AC, U+1F600: 1 + 2 + 3 + 4 bytes.
  const std::string text = "a\xC3\xB1\xE2\x82\xAC\xF0\x9F\x98\x80";
  if (!utf8::is_valid(text.begin(), text.end())) return fail("is_valid");
  if (utf8::distance(text.begin(), text.end()) != 4) return fail("distance");

  std::vector<unsigned short> utf16;
  utf8::utf8to16(text.begin(), text.end(), std::back_inserter(utf16));
  const unsigned short expected16[] = {0x61, 0xF1, 0x20AC, 0xD83D, 0xDE00};
  if (utf16 != std::vector<unsigned short>(expected16, expected16 + 5))
    return fail("utf8to16");
  std::string back;
  utf8::utf16to8(utf16.begin(), utf16.end(), std::back_inserter(back));
  if (back != text) return fail("utf16to8");

  std::vector<unsigned int> utf32;
  utf8::utf8to32(text.begin(), text.end(), std::back_inserter(utf32));
  const unsigned int expected32[] = {0x61, 0xF1, 0x20AC, 0x1F600};
  if (utf32 != std::vector<unsigned int>(expected32, expected32 + 4))
    return fail("utf8to32");

  std::string::const_iterator it = text.end();
  if (utf8::prior(it, text.begin()) != 0x1F600 || it != text.begin() + 6)
    return fail("prior");
  utf8::iterator<std::string::const_iterator> cps(text.begin(), text.begin(),
                                                  text.end());
  ++cps;
  if (*cps != 0xF1) return fail("iterator");

  std::string appended;
  utf8::append(0x1F600, std::back_inserter(appended));
  if (appended != "\xF0\x9F\x98\x80") return fail("append");

  // 0xFF never occurs in UTF-8; the default replacement is U+FFFD.
  const std::string broken = "ab\xFF" "c";
  if (utf8::find_invalid(broken.begin(), broken.end()) != broken.begin() + 2)
    return fail("find_invalid");
  std::string fixed;
  utf8::replace_invalid(broken.begin(), broken.end(),
                        std::back_inserter(fixed));
  if (fixed != "ab\xEF\xBF\xBD" "c") return fail("replace_invalid");

  bool threw = false;
  try {
    std::string::const_iterator bad = broken.begin() + 2;
    utf8::next(bad, broken.end());
  } catch (const utf8::invalid_utf8 &e) {
    threw = e.utf8_octet() == 0xFF;
  }
  if (!threw) return fail("next on an invalid byte");

  // A three-byte sequence cut after two bytes.
  const std::string truncated = "\xE2\x82";
  threw = false;
  try {
    std::string::const_iterator cut = truncated.begin();
    utf8::next(cut, truncated.end());
  } catch (const utf8::not_enough_room &) {
    threw = true;
  }
  if (!threw) return fail("next on a truncated sequence");

  std::string::const_iterator raw = text.begin() + 1;
  if (utf8::unchecked::next(raw) != 0xF1 || raw != text.begin() + 3)
    return fail("unchecked::next");

  std::puts("utfcpp ok");
  return 0;
}
