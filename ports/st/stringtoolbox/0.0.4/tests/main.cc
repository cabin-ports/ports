#include <stringtoolbox.hpp>

#include <cstdio>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char *what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    failures++;
  }
}

using Fields = std::vector<std::string>;

}  // namespace

int main() {
  // Only ' ' and '\t' count as whitespace (stringtoolbox.hpp:38,46), and the
  // functions edit their argument in place and return it.
  std::string a{"  \tHello \t"};
  check(&stringtoolbox::trim(a) == &a, "trim returns its argument");
  check(a == "Hello", "trim strips both ends");

  std::string b{"  x "};
  check(stringtoolbox::ltrim(b) == "x ", "ltrim strips the left only");

  std::string c{"  x "};
  check(stringtoolbox::rtrim(c) == "  x", "rtrim strips the right only");

  std::string d{" A BC "};
  check(stringtoolbox::trim(d) == "A BC", "trim keeps inner whitespace");

  std::string e{"\nx\n"};
  check(stringtoolbox::trim(e) == "\nx\n", "newline is not whitespace");

  std::string w1{" \t "};
  std::string w2{" \t "};
  std::string w3{" \t "};
  check(stringtoolbox::rtrim(w1).empty(), "rtrim of all whitespace");
  check(stringtoolbox::ltrim(w2).empty(), "ltrim of all whitespace");
  check(stringtoolbox::trim(w3).empty(), "trim of all whitespace");
  std::string empty;
  check(stringtoolbox::trim(empty).empty(), "trim of empty input");

  // Upstream test/Test-stringtoolbox.cpp:97-100 and the README example.
  check(stringtoolbox::replaceAll(" A B  C  ", ' ', '_') == "_A_B__C__",
        "replaceAll replaces every occurrence");
  check(stringtoolbox::replaceAll("Hello World.", '.', '!') == "Hello World!",
        "replaceAll with one occurrence");
  const std::string src{"a b"};
  check(stringtoolbox::replaceAll(src, ' ', '-') == "a-b" && src == "a b",
        "replaceAll leaves its argument unchanged");
  check(stringtoolbox::replaceAll("abc", 'x', 'y') == "abc",
        "replaceAll without a match");

  // Upstream test/Test-stringtoolbox.cpp:134-139 (empty fields), :122-126
  // (trailing separator), :104-106 (no separator).
  check(stringtoolbox::split("a,,b", ',') == (Fields{"a", "", "b"}),
        "split keeps an empty middle field");
  check(stringtoolbox::split("a,b,", ',') == (Fields{"a", "b", ""}),
        "split keeps a trailing empty field");
  check(stringtoolbox::split(",a", ',') == (Fields{"", "a"}),
        "split keeps a leading empty field");
  check(stringtoolbox::split(",", ',') == (Fields{"", ""}),
        "split of a lone separator");
  check(stringtoolbox::split("abc", ',') == (Fields{"abc"}),
        "split without the separator");
  check(stringtoolbox::split("", ',').empty(), "split of empty input");

  if (failures == 0) std::puts("ok");
  return failures == 0 ? 0 : 1;
}
