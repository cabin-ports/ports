// Consumer test from C++11: values and errors, access and its failure,
// in-place construction, move-only values, expected<void, E>, swapping
// and comparison, and the P2505 monadic operations.
#include <nonstd/expected.hpp>

#include <cstdio>
#include <memory>
#include <string>
#include <utility>

namespace {

int failures = 0;

void check(bool ok, const char* what, int line) {
  if (!ok) {
    std::fprintf(stderr, "main.cc:%d: check failed: %s\n", line, what);
    ++failures;
  }
}
#define CHECK(cond) check((cond), #cond, __LINE__)

typedef nonstd::expected<int, std::string> Number;

// A decimal digit string as its value, or why it is not one.
Number parse(const std::string& text) {
  if (text.empty()) return nonstd::make_unexpected(std::string("empty"));
  int value = 0;
  for (std::string::size_type i = 0; i < text.size(); ++i) {
    if (text[i] < '0' || text[i] > '9') return nonstd::make_unexpected("not a digit: " + text);
    value = value * 10 + (text[i] - '0');
  }
  return value;
}

Number half(int value) {
  if (value % 2 != 0) return nonstd::make_unexpected(std::string("odd"));
  return value / 2;
}

int twice(int value) { return 2 * value; }
std::size_t length(const std::string& error) { return error.size(); }
Number recover(const std::string&) { return 0; }

nonstd::expected<void, int> close_handle(int handle) {
  if (handle < 0) return nonstd::make_unexpected(-handle);
  return {};
}

}  // namespace

int main() {
  // Values and errors.
  Number good = parse("42");
  Number bad = parse("4x");
  CHECK(good.has_value() && static_cast<bool>(good));
  CHECK(*good == 42 && good.value() == 42);
  CHECK(!bad.has_value() && !bad);
  CHECK(bad.error() == "not a digit: 4x");
  CHECK(bad.value_or(-1) == -1 && good.value_or(-1) == 42);
  CHECK(parse("").error() == "empty");

  // value() on an error throws bad_expected_access carrying the error.
  bool thrown = false;
  try {
    (void)bad.value();
  } catch (const nonstd::bad_expected_access<std::string>& e) {
    thrown = e.error() == "not a digit: 4x";
  }
  CHECK(thrown);

  // Construction in place, and of the error in place.
  nonstd::expected<std::pair<int, int>, int> pair(nonstd::in_place, 3, 4);
  CHECK(pair->first == 3 && pair->second == 4);
  Number made_error(nonstd::unexpect, 3, 'z');
  CHECK(!made_error && made_error.error() == "zzz");
  Number emplaced = bad;
  emplaced.emplace(7);
  CHECK(emplaced && *emplaced == 7);

  // A move-only value.
  nonstd::expected<std::unique_ptr<int>, int> owner(std::unique_ptr<int>(new int(5)));
  nonstd::expected<std::unique_ptr<int>, int> moved(std::move(owner));
  CHECK(moved && **moved == 5);

  // expected<void, E>.
  CHECK(close_handle(3).has_value());
  CHECK(!close_handle(-9) && close_handle(-9).error() == 9);

  // Swapping, and comparison with values and errors.
  Number a = 1, b = nonstd::make_unexpected(std::string("e"));
  a.swap(b);
  CHECK(!a && a.error() == "e" && b && *b == 1);
  CHECK(b == 1 && b != 2);
  CHECK(a == nonstd::make_unexpected(std::string("e")));
  CHECK(parse("12") == parse("12") && parse("12") != parse("13"));

  // Monadic operations (P2505R5, the default).
  CHECK(parse("84").and_then(half).value() == 42);
  CHECK(parse("7").and_then(half).error() == "odd");
  CHECK(parse("x").and_then(half).error() == "not a digit: x");
  CHECK(parse("21").transform(twice).value() == 42);
  CHECK(parse("x").transform_error(length).error() == 14);
  CHECK(parse("x").or_else(recover).value() == 0);
  CHECK(parse("5").or_else(recover).value() == 5);
  CHECK(parse("x").error_or(std::string("none")) == "not a digit: x");
  CHECK(parse("5").error_or(std::string("none")) == "none");

  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("expected-lite consumer test passed");
  return 0;
}
