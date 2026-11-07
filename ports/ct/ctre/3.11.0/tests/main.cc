// Consumer test for cabin-ports/ctre: regular expressions compiled from
// template arguments, matched during constant evaluation and at run time.
#include <ctre.hpp>
#include <ctre-unicode.hpp>

#include <cstdio>
#include <list>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAILED: %s\n", what);
    ++failures;
  }
}

// The whole engine is constexpr.
static_assert(ctre::match<"[a-z]+[0-9]+">("abc123"));
static_assert(!ctre::match<"[a-z]+[0-9]+">("abc"));
static_assert(ctre::search<"[0-9]+">("abc123def").to_view() == "123");

constexpr std::optional<std::string_view> number_after_letters(
    std::string_view s) {
  if (auto m = ctre::match<"[a-z]+([0-9]+)">(s)) return m.get<1>().to_view();
  return std::nullopt;
}
static_assert(number_after_letters("abc42") == std::string_view("42"));
static_assert(!number_after_letters("42abc"));

// README's C++17 spelling (README.md:138-150), which C++20 accepts too: a
// pattern held in a ctll::fixed_string variable.
static constexpr auto hello_pattern = ctll::fixed_string{"h.*o"};

void test_match() {
  const std::string input = std::string("key") + "=" + "value";
  auto m = ctre::match<"([a-z]+)=([a-z]+)">(input);
  check(static_cast<bool>(m), "match accepts the whole input");
  check(m.get<1>().to_view() == "key" && m.get<2>().to_view() == "value",
        "match captures by index");
  check(m.to_view() == input && m.str() == input,
        "capture 0 is the whole match");
  static_assert(decltype(m)::count() == 3, "two groups plus the whole match");
  auto [whole, key, value] = m;
  check(whole.to_view() == input && key.to_view() == "key" &&
            value.to_view() == "value",
        "results decompose into structured bindings");

  check(!ctre::match<"[a-z]+">(std::string("abc1")),
        "match rejects a partial match");
  check(ctre::starts_with<"[a-z]+">(std::string("abc1")).to_view() == "abc",
        "starts_with accepts a prefix");
  check(!ctre::starts_with<"GET|POST">(std::string("PUT /")),
        "starts_with rejects a non-matching prefix");
  check(ctre::match<hello_pattern>(std::string("hello")),
        "a fixed_string variable works as the pattern");

  constexpr auto is_hex = ctre::match<"0x[0-9a-fA-F]+">;
  check(is_hex(std::string("0x1F")) && !is_hex(std::string("0x1G")),
        "a matcher is a reusable function object");

  const std::vector<char> chars{'x', 'y', 'z'};
  check(ctre::match<"x.z">(chars.begin(), chars.end()),
        "match takes an iterator pair");
  const std::list<char> linked{'a', 'b', 'b', 'a'};
  check(ctre::match<"a(b+)a">(linked.begin(), linked.end()),
        "match takes forward iterators over a non-contiguous range");
}

void test_search() {
  const std::string text = "Released on 2026-05-08 at noon";
  auto m = ctre::search<"(?<year>\\d{4})-(?<month>\\d{2})-(?<day>\\d{2})">(
      text);
  check(static_cast<bool>(m), "search finds a match inside the input");
  check(m.get<"year">().to_view() == "2026" &&
            m.get<"day">().to_view() == "08",
        "search captures by name");
  check(m.get<"month">().to_number() == 5, "a capture converts to a number");
  check(m.to_view().data() == text.data() + 12,
        "the match view points into the input");

  // Results hold iterators into the input, which must outlive them.
  const std::string bare = "id", numbered = "id:17";
  auto opt = ctre::match<"id(?::(\\d+))?">(bare);
  check(opt && !opt.get<1>() && !opt.get<1>().to_optional_number(),
        "an unmatched optional group is empty");
  auto some = ctre::match<"id(?::(\\d+))?">(numbered);
  check(some.get<1>().to_optional_number() == std::optional<int>(17),
        "a matched optional group converts to a number");

  check(ctre::search<"<.+?>">(std::string("<a><b>")).to_view() == "<a>",
        "a lazy quantifier stops early");
  check(ctre::search<"<.+>">(std::string("<a><b>")).to_view() == "<a><b>",
        "a greedy quantifier runs to the end");
  check(ctre::match<"\\d{2,3}">(std::string("123")) &&
            !ctre::match<"\\d{2,3}">(std::string("1234")),
        "a bounded repetition counts");
  check(ctre::match<"(a+)b\\1">(std::string("aabaa")) &&
            !ctre::match<"(a+)b\\1">(std::string("aaba")),
        "a backreference repeats the captured text");

  const std::string words = "foobaz foobar";
  auto ahead = ctre::search<"foo(?=bar)">(words);
  check(ahead.to_view() == "foo" && ahead.to_view().data() == words.data() + 7,
        "a lookahead constrains without consuming");
}

void test_modes() {
  const std::string lines = "a\nb\nc";
  check(!ctre::search<"^b$">(lines), "^ and $ anchor the input by default");
  check(ctre::multiline_search<"^b$">(lines).to_view() == "b",
        "multiline ^ and $ anchor each line");
  check(ctre::match<"hello", ctre::case_insensitive>(std::string("HeLLo")),
        "the case_insensitive modifier ignores case");
  check(!ctre::match<"hello">(std::string("HeLLo")),
        "matching is case-sensitive by default");
  check(ctre::match<"(?i)hello">(std::string("HELLO")),
        "an inline (?i) mode ignores case");
}

void test_ranges() {
  const std::string text = "a1 b22 c333 d";
  std::vector<int> numbers;
  for (auto m : ctre::search_all<"[0-9]+">(text)) {
    numbers.push_back(m.to_number());
  }
  check(numbers == std::vector<int>{1, 22, 333},
        "search_all yields every match");

  std::vector<std::string> fields;
  for (auto part : ctre::split<",">(std::string_view("a,b,,c"))) {
    fields.push_back(part.str());
  }
  check(fields == std::vector<std::string>{"a", "b", "", "c"},
        "split yields the text between separators");

  std::vector<std::string> tokens;
  for (auto t : ctre::tokenize<"[0-9]+|[a-z]+">(std::string_view("123abc45"))) {
    tokens.push_back(t.str());
  }
  check(tokens == std::vector<std::string>{"123", "abc", "45"},
        "tokenize yields consecutive matches");

  tokens.clear();
  for (auto t : ctre::tokenize<"[0-9]+">(std::string_view("12-34"))) {
    tokens.push_back(t.str());
  }
  check(tokens == std::vector<std::string>{"12"},
        "tokenize stops at text it cannot match");
}

void test_unicode() {
  check(ctre::match<"\\p{Lu}+">(std::u32string_view(U"\u00C4\u00D6")),
        "\\p{Lu} matches uppercase letters beyond ASCII");
  check(!ctre::match<"\\p{Lu}+">(std::u32string_view(U"\u00E4")),
        "\\p{Lu} rejects a lowercase letter");
  auto greek = ctre::search<"\\p{script=Greek}+">(
      std::u32string_view(U"abc \u03B1\u03B2\u03B3 def"));
  check(greek.to_view() == U"\u03B1\u03B2\u03B3",
        "a script property selects Greek letters");

  // UTF-8 input is decoded: one code point may span several bytes.
  const std::u8string_view word = u8"g\u00E9nie";
  check(ctre::match<"\\p{Letter}{5}">(word),
        "UTF-8 input is matched per code point");
  check(!ctre::match<"\\p{Letter}{6}">(word),
        "a two-byte letter counts once");
}

}  // namespace

int main() {
  test_match();
  test_search();
  test_modes();
  test_ranges();
  test_unicode();
  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("ctre consumer ok");
  return 0;
}
