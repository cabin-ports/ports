// Exercises bpstd's string_view, optional, variant, any, span, byte and
// exchange with exact expected values.  It never calls string_view::cend() or
// crend() (cabin.toml).

#include <bpstd/any.hpp>
#include <bpstd/cstddef.hpp>
#include <bpstd/optional.hpp>
#include <bpstd/span.hpp>
#include <bpstd/string_view.hpp>
#include <bpstd/utility.hpp>
#include <bpstd/variant.hpp>

#include <cstdio>
#include <sstream>
#include <string>

static int failures = 0;

static void check(bool ok, const char *what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

struct describe {
  std::string operator()(int i) const { return "int " + std::to_string(i); }
  std::string operator()(const std::string &s) const { return "string " + s; }
};

static void test_string_view() {
  bpstd::string_view sv("hello world");
  check(sv.size() == 11, "string_view size");
  check(sv.substr(6) == bpstd::string_view("world"), "string_view substr");
  check(sv.find('o') == 4 && sv.rfind('o') == 7, "string_view find/rfind");
  check(sv.find("xyz") == bpstd::string_view::npos, "string_view find missing");
  check(static_cast<std::string>(sv.substr(0, 5)) == "hello", "string_view to std::string");
  check(sv.compare(bpstd::string_view("hello")) > 0, "string_view compare");
  std::string seen(sv.begin(), sv.end());
  check(seen == "hello world", "string_view iteration");
  std::ostringstream os;
  os << sv.substr(0, 5) << '|';
  check(os.str() == "hello|", "string_view operator<<");
}

static void test_optional() {
  bpstd::optional<int> o;
  check(!o.has_value() && o.value_or(7) == 7, "empty optional");
  o = 5;
  check(o.has_value() && *o == 5 && o.value() == 5, "engaged optional");
  o = bpstd::nullopt;
  check(!o, "optional reset by nullopt");
}

static void test_variant() {
  bpstd::variant<int, std::string> v(std::string("x"));
  check(v.index() == 1 && bpstd::holds_alternative<std::string>(v), "variant holds string");
  check(bpstd::get<std::string>(v) == "x", "variant get<string>");
  check(bpstd::visit(describe{}, v) == "string x", "visit string");
  v = 3;
  check(v.index() == 0 && bpstd::get<int>(v) == 3, "variant holds int");
  check(bpstd::get_if<std::string>(&v) == nullptr, "get_if wrong alternative");
  check(bpstd::visit(describe{}, v) == "int 3", "visit int");
}

static void test_any() {
  bpstd::any a = 1;
  check(a.has_value() && bpstd::any_cast<int>(a) == 1, "any holds int");
  a = std::string("text");
  check(bpstd::any_cast<std::string>(&a) != nullptr && *bpstd::any_cast<std::string>(&a) == "text",
        "any holds string");
  check(bpstd::any_cast<int>(&a) == nullptr, "any_cast wrong type");
  bool threw = false;
  try {
    (void)bpstd::any_cast<int>(a);
  } catch (const bpstd::bad_any_cast &) {
    threw = true;
  }
  check(threw, "any_cast throws bad_any_cast");
}

static void test_span_and_byte() {
  int arr[4] = {1, 2, 3, 4};
  bpstd::span<int> s(arr);
  check(s.size() == 4 && s[2] == 3, "span over array");
  bpstd::span<int> tail = s.subspan(1);
  check(tail.size() == 3 && tail.front() == 2 && tail.back() == 4, "span subspan");
  tail[0] = 20;
  check(arr[1] == 20, "span refers to the array");

  bpstd::byte b = static_cast<bpstd::byte>(0x0f);
  b = (b << 4) | static_cast<bpstd::byte>(0x01);
  check(bpstd::to_integer<int>(b) == 0xf1, "byte shift and or");

  int x = 1;
  check(bpstd::exchange(x, 2) == 1 && x == 2, "exchange");
}

int main() {
  test_string_view();
  test_optional();
  test_variant();
  test_any();
  test_span_and_byte();
  if (failures != 0) {
    return 1;
  }
  std::puts("backport-cpp 1.2.0 C++ ok");
  return 0;
}
