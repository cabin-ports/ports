#include <stringzilla/stringzilla.hpp>

#include <cstdio>
#include <string>
#include <vector>

namespace sz = ashvardanian::stringzilla;

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

void check_view() {
  sz::string_view text = "hello world, hello stringzilla";
  CHECK(text.size() == 30);
  CHECK(text.find("hello") == 0 && text.rfind("hello") == 13);
  CHECK(text.find("hello", 1) == 13);
  CHECK(text.find("xyz") == sz::string_view::npos);
  CHECK(text.find('w') == 6 && text.rfind('o') == 17);
  CHECK(text.contains("zilla") && !text.contains("Zilla"));
  CHECK(text.starts_with("hello") && text.ends_with("zilla"));
  CHECK(text.substr(6, 5) == "world");

  auto parts = text.partition(", ");
  CHECK(parts.before == "hello world" && parts.match == ", " &&
        parts.after == "hello stringzilla");
}

void check_split() {
  sz::string_view csv = "a,bb,,ccc";
  std::vector<std::string> fields;
  for (auto field : csv.split(","))
    fields.push_back(std::string(field.data(), field.size()));
  CHECK((fields == std::vector<std::string>{"a", "bb", "", "ccc"}));

  std::vector<std::string> reversed;
  for (auto field : csv.rsplit(","))
    reversed.push_back(std::string(field.data(), field.size()));
  CHECK((reversed == std::vector<std::string>{"ccc", "", "bb", "a"}));

  std::vector<std::string> trailing;
  for (auto field : sz::string_view("a,").split(","))
    trailing.push_back(std::string(field.data(), field.size()));
  CHECK((trailing == std::vector<std::string>{"a", ""}));

  std::size_t matches = 0;
  for (auto match : sz::string_view("aaaa").find_all("aa")) {
    CHECK(match == "aa");
    ++matches;
  }
  CHECK(matches == 3);
}

void check_string() {
  // Growing from 5 to 48 bytes crosses the inline (small-string) capacity.
  sz::string s = "short";
  CHECK(s.size() == 5 && s == "short");
  s += " and now much longer than the inline buffer";
  CHECK(s.size() == 48 && s.ends_with("buffer"));
  CHECK(s.try_replace_all("n", "N"));
  CHECK(s == "short aNd Now much loNger thaN the iNliNe buffer");
  CHECK(s.try_replace_all("N", "<n>"));
  CHECK(s == "short a<n>d <n>ow much lo<n>ger tha<n> the i<n>li<n>e buffer");
  CHECK(s.try_replace_all("<n>", "N"));
  CHECK(s == "short aNd Now much loNger thaN the iNliNe buffer");
  sz::string copy = s;
  CHECK(copy == s && copy.data() != s.data());
  CHECK(copy.hash() == s.hash());
  s.clear();
  CHECK(s.empty());
}

void check_hash_and_unicode() {
  sz::string_view fox = "The quick brown fox jumps over the lazy dog";
  CHECK(fox.hash() == static_cast<std::size_t>(sz_hash(fox.data(), fox.size(), 0)));
  CHECK(fox.hash(7) == static_cast<std::size_t>(sz_hash(fox.data(), fox.size(), 7)));
  CHECK(fox.hash(7) != fox.hash(8));
  CHECK(sz::string_view("h\xC3\xA9llo").utf8_count() == 5);
}

} // namespace

int main() {
  check_view();
  check_split();
  check_string();
  check_hash_and_unicode();
  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("stringzilla c++ ok");
  return 0;
}
