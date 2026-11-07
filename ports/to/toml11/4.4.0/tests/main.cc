// Consumer test for cabin-ports/toml11: parsing a TOML 1.0 document, typed
// lookups and container conversions, comments and number formats kept
// through serialization, error reporting with locations, the TOML 1.1
// features behind toml::spec, and user-defined conversions.
#include <toml.hpp>

#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

static_assert(TOML11_VERSION_MAJOR == 4 && TOML11_VERSION_MINOR == 4 && TOML11_VERSION_PATCH == 0,
              "toml11 4.4.0");

namespace app {

struct endpoint {
  std::string host;
  std::vector<int> ports;
};

struct point {
  int x;
  int y;
};

}  // namespace app

// A hand-written conversion, which every compiler accepts.
namespace toml {

template <> struct from<app::endpoint> {
  template <typename TC> static app::endpoint from_toml(const basic_value<TC> &v) {
    return app::endpoint{find<std::string>(v, "host"), find<std::vector<int>>(v, "ports")};
  }
};

template <> struct into<app::endpoint> {
  template <typename TC> static basic_value<TC> into_toml(const app::endpoint &e) {
    using table_type = typename basic_value<TC>::table_type;
    using array_type = typename basic_value<TC>::array_type;
    array_type ports;
    for (int p : e.ports) ports.push_back(basic_value<TC>(p));
    return basic_value<TC>(table_type{{"host", e.host}, {"ports", ports}});
  }
};

}  // namespace toml

// TOML11_DEFINE_CONVERSION_NON_INTRUSIVE needs a conforming preprocessor:
// upstream gives MSVC consumers /Zc:preprocessor and drops the macro where
// there is none.  A Cabin manifest cannot pass the flag, so the macro is
// tested only where _MSVC_TRADITIONAL is unset or 0; the output says which.
#if !defined(_MSVC_TRADITIONAL) || !_MSVC_TRADITIONAL
#define TEST_NON_INTRUSIVE 1
TOML11_DEFINE_CONVERSION_NON_INTRUSIVE(app::point, x, y)
#else
#define TEST_NON_INTRUSIVE 0
#endif

namespace {

int failures = 0;

void check(bool ok, const char *what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

bool contains(const std::string &s, const char *part) {
  return s.find(part) != std::string::npos;
}

const char *const document = R"(# settings
title = "example"
port = 0x1F90 # hex is kept
ratio = 0.5
enabled = true
nums = [3, 1, 4, 1, 5]
when = 1979-05-27T07:32:00-08:00
day = 2026-11-07

[owner]
)"
    // An ordinary literal, so the TOML parser and not the compiler decodes the escape.
    "name = \"Tom \\u00C9\"\n"
    R"(dotted.key = 'C:\path'

[[servers]]
host = "alpha"
ports = [8001, 8002]

[[servers]]
host = "beta"
ports = [9001]

[point]
x = 1
y = -2
)";

void test_lookup(const toml::value &data) {
  check(toml::find<std::string>(data, "title") == "example", "string");
  check(toml::find<int>(data, "port") == 8080, "hex integer");
  check(toml::find<double>(data, "ratio") == 0.5, "float");
  check(toml::find<bool>(data, "enabled"), "boolean");
  check(toml::find<std::vector<int>>(data, "nums") == std::vector<int>{3, 1, 4, 1, 5},
        "array to std::vector");
  // \u00C9 is U+00C9, UTF-8 C3 89.
  check(toml::find<std::string>(data, "owner", "name") == "Tom \xC3\x89", "unicode escape");
  check(toml::find<std::string>(data, "owner", "dotted", "key") == "C:\\path",
        "dotted key and literal string");
  check(toml::find_or<int>(data, "missing", 42) == 42, "find_or fallback");
  check(!data.contains("missing") && data.at("owner").is_table(), "contains / is_table");

  const toml::offset_datetime &when = data.at("when").as_offset_datetime();
  check(when.date.year == 1979 && when.date.month == 4 && when.date.day == 27,
        "datetime date (month is 0-based)");
  check(when.time.hour == 7 && when.time.minute == 32 && when.offset.hour == -8,
        "datetime time and offset");
  const toml::local_date &day = data.at("day").as_local_date();
  check(day.year == 2026 && day.month == 10 && day.day == 7, "local date");

  const toml::array &servers = data.at("servers").as_array();
  check(servers.size() == 2, "array of tables");
  app::endpoint beta = toml::get<app::endpoint>(servers.at(1));
  check(beta.host == "beta" && beta.ports == std::vector<int>{9001}, "from<> conversion");
  std::vector<app::endpoint> all = toml::find<std::vector<app::endpoint>>(data, "servers");
  check(all.size() == 2 && all[0].ports.size() == 2 && all[0].ports[1] == 8002,
        "vector of user types");
}

void test_errors(const toml::value &data) {
  try {
    (void)toml::find<int>(data, "title");
    check(false, "a type mismatch throws");
  } catch (const toml::type_error &e) {
    check(contains(e.what(), "bad_cast to integer"), "type_error message");
  }
  try {
    (void)toml::find<int>(data, "missing");
    check(false, "a missing key throws");
  } catch (const std::out_of_range &e) {
    check(contains(e.what(), "key \"missing\" not found"), "missing key message");
  }

  // The throwing parser reports every error with its location.
  try {
    (void)toml::parse_str("a = 1\nb = [1, 2\n");
    check(false, "an unterminated array throws");
  } catch (const toml::syntax_error &e) {
    check(!e.errors().empty(), "syntax_error carries errors");
  }

  // The non-throwing parser returns them instead.
  auto dup = toml::try_parse_str("a = 1\na = 2\n");
  check(dup.is_err(), "a duplicate key fails");
  if (dup.is_err()) {
    const std::vector<toml::error_info> &errs = dup.unwrap_err();
    check(errs.size() == 1 && contains(errs[0].title(), "already exists"),
          "duplicate key title");
    bool second_line = false;
    for (const auto &loc : errs[0].locations())
      if (loc.first.first_line_number() == 2) second_line = true;
    check(second_line, "duplicate key located on line 2");
  }
}

void test_serialization(toml::value data) {
  check(data.at("port").as_integer_fmt().fmt == toml::integer_format::hex, "hex format kept");
  check(data.at("port").comments().size() == 1 &&
            data.at("port").comments().at(0) == "# hex is kept",
        "inline comment kept");

  data["nums"].as_array().push_back(9);
  data["added"] = toml::table{{"list", toml::array{1, 2}}, {"flag", false}};
  const std::string text = toml::format(data);
  check(contains(text, "0x1F90") && contains(text, "# hex is kept"),
        "format keeps the hex literal and the comment");

  const toml::value again = toml::parse_str(text);
  check(toml::find<std::vector<int>>(again, "nums").size() == 6, "edited array round trip");
  check(toml::find<std::vector<int>>(again, "added", "list") == std::vector<int>{1, 2} &&
            !toml::find<bool>(again, "added", "flag"),
        "new table round trip");
  check(toml::find<std::string>(again, "owner", "name") == "Tom \xC3\x89", "UTF-8 round trip");
  check(again.at("when").as_offset_datetime() == data.at("when").as_offset_datetime(),
        "datetime round trip");

  app::endpoint e{"gamma", {1, 2, 3}};
  const toml::value v(e);
  const app::endpoint back = toml::get<app::endpoint>(toml::parse_str(toml::format(v)));
  check(back.host == "gamma" && back.ports == e.ports, "into<> conversion round trip");
}

void test_spec() {
  // TOML 1.1 features are off by default and enabled through toml::spec.
  const char *trailing = "a = { x = 1, y = 2, }\n";
  check(toml::try_parse_str(trailing).is_err(), "TOML 1.0 rejects a trailing comma");
  auto v11 = toml::try_parse_str(trailing, toml::spec::v(1, 1, 0));
  check(v11.is_ok() && toml::find<int>(v11.unwrap(), "a", "y") == 2,
        "TOML 1.1 accepts a trailing comma");

  const char *escape = "s = \"\\e[0m\"\n";
  check(toml::try_parse_str(escape).is_err(), "TOML 1.0 rejects \\e");
  auto e11 = toml::try_parse_str(escape, toml::spec::v(1, 1, 0));
  check(e11.is_ok() && toml::find<std::string>(e11.unwrap(), "s") == "\x1b[0m",
        "TOML 1.1 \\e escape");
}

void test_non_intrusive(const toml::value &data) {
#if TEST_NON_INTRUSIVE
  const app::point p = toml::find<app::point>(data, "point");
  check(p.x == 1 && p.y == -2, "generated from_toml");
  const toml::value v(app::point{3, 4});
  check(toml::find<int>(v, "x") == 3 && toml::find<int>(v, "y") == 4, "generated into_toml");
#else
  (void)data;
#endif
}

}  // namespace

int main() {
  const toml::value data = toml::parse_str(document);
  test_lookup(data);
  test_errors(data);
  test_serialization(data);
  test_spec();
  test_non_intrusive(data);
  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::printf("toml11 consumer ok (non-intrusive macro %s)\n",
              TEST_NON_INTRUSIVE ? "tested" : "skipped");
  return 0;
}
