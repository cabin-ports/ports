// Consumer test from C++98: construction, element access and its failure,
// prefix/suffix removal, substr, copy, compare, every find family member
// (including an empty needle at the end, fixed in 1.8.2), comparison with
// C strings and std::string, conversion to and from std::string, and padded
// stream insertion; from C++11 on, the _sv literal, std::hash, and constexpr
// construction.
#include <nonstd/string_view.hpp>

#include <cstdio>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#if nssv_CPP11_OR_GREATER
#include <functional>
#endif

namespace {

int failures = 0;

void check(bool ok, const char* what, int line) {
  if (!ok) {
    std::fprintf(stderr, "main.cc:%d: check failed: %s\n", line, what);
    ++failures;
  }
}
#define CHECK(cond) check((cond), #cond, __LINE__)

typedef nonstd::string_view sv;

// Splits on a separator and returns the field at the given index.
sv field(sv text, char sep, int index) {
  for (; index > 0; --index) {
    sv::size_type at = text.find(sep);
    if (at == sv::npos) return sv();
    text.remove_prefix(at + 1);
  }
  return text.substr(0, text.find(sep));
}

}  // namespace

int main() {
  CHECK(std::string(string_view_lite_VERSION) == "1.8.2");

  sv empty;
  CHECK(empty.empty() && empty.size() == 0);

  const char text[] = "key=value;other";
  sv whole(text);
  CHECK(whole.size() == 15 && whole.length() == 15);
  CHECK(whole.data() == text);
  CHECK(whole.front() == 'k' && whole.back() == 'r');
  CHECK(whole[4] == 'v' && whole.at(14) == 'r');

  bool threw = false;
  try {
    (void)whole.at(15);
  } catch (const std::out_of_range&) {
    threw = true;
  }
  CHECK(threw);

  sv counted(text, 3);
  CHECK(counted == "key");
  CHECK(counted.size() == 3 && counted.data() == text);

  sv trimmed = whole;
  trimmed.remove_prefix(4);
  trimmed.remove_suffix(6);
  CHECK(trimmed == "value");

  CHECK(whole.substr(10) == "other");
  CHECK(whole.substr(4, 5) == "value");
  CHECK(whole.substr(15).empty());
  threw = false;
  try {
    (void)whole.substr(16);
  } catch (const std::out_of_range&) {
    threw = true;
  }
  CHECK(threw);

  char buffer[8] = {0};
  CHECK(whole.copy(buffer, 5, 4) == 5);
  CHECK(std::string(buffer) == "value");

  CHECK(sv("abc").compare(sv("abd")) < 0);
  CHECK(sv("abd").compare(sv("abc")) > 0);
  CHECK(sv("abc").compare(sv("abc")) == 0);
  CHECK(sv("ab").compare(sv("abc")) < 0);
  CHECK(whole.compare(4, 5, "value") == 0);

  CHECK(whole.find('=') == 3);
  CHECK(whole.find("value") == 4);
  CHECK(whole.find('e', 2) == 8);
  CHECK(whole.find('z') == sv::npos);
  CHECK(whole.rfind('e') == 13);
  CHECK(whole.rfind("e", 12) == 8);
  CHECK(whole.find_first_of("=;") == 3);
  CHECK(whole.find_last_of("=;") == 9);
  CHECK(whole.find_first_not_of("key") == 3);
  CHECK(whole.find_last_not_of("other") == 9);
  CHECK(sv("abc").find(sv(""), 3) == 3);
  CHECK(sv("abc").find(sv(""), 4) == sv::npos);
  CHECK(sv("abc").rfind(sv("")) == 3);

  CHECK(field(whole, ';', 0) == "key=value");
  CHECK(field(whole, ';', 1) == "other");
  CHECK(field(whole, ';', 2).empty());

  std::string owned("value");
  CHECK(trimmed == owned && owned == trimmed);
  CHECK(sv("abc") < sv("abd") && sv("abd") > sv("abc"));
  CHECK(sv("abc") != "abd" && "abc" == sv("abc"));
  CHECK(sv("abc") <= "abc" && sv("abc") >= "abc");

  sv viewed = nonstd::to_string_view(owned);
  CHECK(viewed.data() == owned.data() && viewed == "value");
  std::string copied = nonstd::to_string(trimmed);
  CHECK(copied == "value" && copied.data() != trimmed.data());

  std::ostringstream out;
  out << '[' << std::right << std::setw(7) << trimmed << ']';
  out << '[' << std::left << std::setfill('.') << std::setw(7) << counted << ']';
  out << '[' << std::setw(2) << trimmed << ']' << trimmed;
  CHECK(out.str() == "[  value][key....][value]value");

#if nssv_CPP11_OR_GREATER
  using namespace nonstd::literals;
  sv literal = "a\0b"_sv;
  CHECK(literal.size() == 3 && literal[1] == '\0');

  CHECK(std::hash<sv>()(trimmed) == std::hash<std::string>()(owned));

  constexpr sv fixed("fixed", 3);
  static_assert(fixed.size() == 3, "constexpr size");
  CHECK(fixed == "fix");

  std::string explicitly(trimmed);
  CHECK(explicitly == "value");
#endif

  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("string-view-lite consumer test passed");
  return 0;
}
