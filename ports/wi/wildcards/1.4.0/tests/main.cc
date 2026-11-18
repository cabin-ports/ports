#include <wildcards.hpp>

#include <cstdio>
#include <string>

namespace {

int failures = 0;

#define CHECK(cond)                                                            \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__,    \
                   #cond);                                                     \
      ++failures;                                                              \
    }                                                                          \
  } while (0)

using wildcards::match;

// README.md:11-36: matching is usable in constant expressions.
static_assert(match("source.c", "*.[hc](pp|)"), "");
static_assert(!match("source.cc", "*.[hc](pp|)"), "");

struct case_insensitive {
  bool operator()(char a, char b) const { return lower(a) == lower(b); }
  static char lower(char c) {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
  }
};

bool matches(char const *s, char const *p) {
  return static_cast<bool>(match(std::string(s), std::string(p)));
}

void check_runtime() {
  CHECK(match("Hello, World!", "H?llo,*W*!"));
  CHECK(match("Hallo, crazy WildCards!", "H?llo,*W*!"));
  CHECK(!match("Hllo, World!", "H?llo,*W*!"));
  CHECK(!match("Hello, world!", "H?llo,*W*!"));

  CHECK(matches("main.hpp", "*.[!c]pp"));
  CHECK(!matches("main.cpp", "*.[!c]pp"));
  CHECK(matches("x.h", "(*.hpp|*.h)"));
  CHECK(matches("x.hpp", "(*.hpp|*.h)"));
  CHECK(!matches("x.c", "(*.hpp|*.h)"));
  CHECK(!matches("main.hpp", "*.[!ch]pp"));
  CHECK(matches("x.hpp", "x.(h(pp|xx)|c)"));
  CHECK(!matches("x.hh", "x.(h(pp|xx)|c)"));
  CHECK(matches("abc", "a*"));
  CHECK(!matches("cab", "a*"));
  CHECK(matches("ab", "a*b"));
  // The whole sequence must match, not a prefix.
  CHECK(!matches("abcd", "abc"));
  CHECK(!matches("x.hpp", "*.h"));

  // A backslash escapes a special character; a set takes it literally.
  CHECK(matches("a*b", "a\\*b"));
  CHECK(!matches("axb", "a\\*b"));
  CHECK(matches("?", "[?]"));
  CHECK(!matches("x", "[?]"));
  CHECK(matches("a)", "(a\\)|b)"));

  // The standard cards leave sets and alternatives off.
  wildcards::cards_type const standard = wildcards::cards_type::standard;
  CHECK(match(std::string("[ab]"), std::string("[ab]"), standard));
  CHECK(!match(std::string("a"), std::string("[ab]"), standard));
  CHECK(match(std::string("(a|b)"), std::string("(a|b)"), standard));
  CHECK(!match(std::string("a"), std::string("(a|b)"), standard));

  CHECK(match(std::string("README.MD"), std::string("readme.?d"),
              case_insensitive()));
  CHECK(!match(std::string("README.MD"), std::string("readme.?d")));

  CHECK(match(L"file.txt", L"*.txt"));
  CHECK(match(L"a.c", L"?.c"));
  CHECK(match(L"file.tst", L"*.t[!x]t"));
  CHECK(!match(L"file.txt", L"*.t[!x]t"));
  CHECK(match(U"\u00e9t\u00e9", U"?t?"));
}

} // namespace

int main() {
  CHECK(WILDCARDS_VERSION_MAJOR == 1 && WILDCARDS_VERSION_MINOR == 4 &&
        WILDCARDS_VERSION_PATCH == 0);
  check_runtime();
  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("wildcards ok");
  return 0;
}
