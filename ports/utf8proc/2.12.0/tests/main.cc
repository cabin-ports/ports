// utf8proc's C API from C++11: grapheme segmentation with
// utf8proc_iterate and utf8proc_grapheme_break_stateful, and
// normalization through utf8proc_map.
#include <cstdio>
#include <string>

#include <utf8proc.h>

namespace {

int failures = 0;

void check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "main.cc: check failed: %s\n", what);
    ++failures;
  }
}

const utf8proc_uint8_t* bytes(const std::string& s) {
  return reinterpret_cast<const utf8proc_uint8_t*>(s.data());
}

// The number of extended grapheme clusters in `s`, or -1 if it is not
// valid UTF-8.
int count_graphemes(const std::string& s) {
  utf8proc_ssize_t pos = 0;
  utf8proc_ssize_t len = static_cast<utf8proc_ssize_t>(s.size());
  utf8proc_int32_t prev = -1;
  utf8proc_int32_t state = 0;
  int count = 0;
  while (pos < len) {
    utf8proc_int32_t cp;
    utf8proc_ssize_t n = utf8proc_iterate(bytes(s) + pos, len - pos, &cp);
    if (n < 0) return -1;
    if (prev < 0 || utf8proc_grapheme_break_stateful(prev, cp, &state)) {
      ++count;
    }
    prev = cp;
    pos += n;
  }
  return count;
}

std::string map(const std::string& s, unsigned int options) {
  utf8proc_uint8_t* out = nullptr;
  utf8proc_ssize_t n = utf8proc_map(
      bytes(s), static_cast<utf8proc_ssize_t>(s.size()), &out, options);
  if (n < 0) return "<" + std::string(utf8proc_errmsg(n)) + ">";
  std::string result(reinterpret_cast<char*>(out), static_cast<size_t>(n));
  utf8proc_free(out);
  return result;
}

}  // namespace

int main() {
  // e + COMBINING ACUTE, the flag of Japan, and a ZWJ family emoji
  // (U+1F469 U+200D U+1F469 U+200D U+1F467): three clusters.
  check(count_graphemes("e\xCC\x81"
                        "\xF0\x9F\x87\xAF\xF0\x9F\x87\xB5"
                        "\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x91\xA9"
                        "\xE2\x80\x8D\xF0\x9F\x91\xA7") == 3,
        "three grapheme clusters");
  check(count_graphemes("abc") == 3, "ASCII clusters");
  check(count_graphemes("\xE0\x80") == -1, "invalid UTF-8 rejected");

  // U+1E9B U+0323 under NFKC (UAX #15's example): U+1E69.
  check(map("\xE1\xBA\x9B\xCC\xA3", UTF8PROC_STABLE | UTF8PROC_COMPOSE |
                                        UTF8PROC_COMPAT) == "\xE1\xB9\xA9",
        "NFKC of U+1E9B U+0323");
  check(map("\xC3\x85ngstr\xC3\xB6m", UTF8PROC_STABLE | UTF8PROC_DECOMPOSE |
                                          UTF8PROC_CASEFOLD) ==
            "a\xCC\x8Angstro\xCC\x88m",
        "casefolded NFD");
  check(map("a\xFF", UTF8PROC_COMPOSE) == "<Invalid UTF-8 string>",
        "invalid UTF-8 error message");

  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  return 0;
}
