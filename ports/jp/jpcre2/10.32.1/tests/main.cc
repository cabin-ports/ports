#include <jpcre2.hpp>

#include <cstdio>
#include <cstring>

namespace {

int failures = 0;

void check(bool ok, const char *what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    failures++;
  }
}

typedef jpcre2::select<char> jp;
typedef jpcre2::select<wchar_t> jw;

}  // namespace

int main() {
  // Numbered substrings of a global match.
  jp::Regex re("(\\d+)-(\\d+)");
  check(re.getErrorNumber() == 0, "valid pattern compiles");
  jp::VecNum vn;
  size_t n = jp::RegexMatch(&re)
                 .setSubject("10-20 30-40")
                 .addModifier("g")
                 .setNumberedSubstringVector(&vn)
                 .match();
  check(n == 2, "two global matches");
  check(vn.size() == 2, "two match vectors");
  if (vn.size() == 2) {
    check(vn[0][0] == "10-20", "first match");
    check(vn[0][1] == "10", "first group of first match");
    check(vn[0][2] == "20", "second group of first match");
    check(vn[1][0] == "30-40", "second match");
    check(vn[1][1] == "30", "first group of second match");
    check(vn[1][2] == "40", "second group of second match");
  }

  // Replacement.
  check(jp::Regex("\\d+").replace("a1b22c", "#", "g") == "a#b#c", "replace");

  // Invalid pattern.
  jp::Regex bad("[");
  check(bad.getErrorNumber() != 0, "invalid pattern reports an error");
  check(jp::RegexMatch(&bad).setSubject("[").match() == 0,
        "no match against an uncompiled regex");

  // Wide characters (16-bit PCRE2 on Windows, 32-bit elsewhere).
  jw::Regex wre(L"\\w+");
  check(wre.getErrorNumber() == 0, "wide pattern compiles");
  check(wre.match(L"ab cd ef", "g") == 3, "wide match count");

  return failures == 0 ? 0 : 1;
}
