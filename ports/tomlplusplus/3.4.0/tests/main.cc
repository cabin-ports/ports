// The C++ consumer: parsing a document with every TOML value type
// (integers in four bases, special floats, the four string forms, offset
// and local date-times, arrays, inline tables, dotted keys and arrays of
// tables), typed access through node views and paths, parse errors with
// their descriptions and positions, parse_file, building and editing a
// table, and the TOML and JSON formatters.
#include <toml++/toml.hpp>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>

using namespace std::string_view_literals;

namespace {

bool ok = true;

void check(bool pass, const char* what) {
  if (!pass) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ok = false;
  }
}

constexpr std::string_view document = R"(# An original inventory document.
title = "Inventory"
revision = 42
largest = 9_223_372_036_854_775_807
hex = 0xDEAD_BEEF
octal = 0o755
binary = 0b1101
ratio = 0.125
avogadro = 6.02e23
falling = -inf
unknown = nan
ready = true
literal = 'C:\temp\no\escapes'
escaped = "tab\there \u00e9 \U0001F600"
multiline = """
first line
second line"""
raw_multiline = '''
kept \n as written'''
stamp = 2001-09-09T01:46:40+05:30
west = 2001-09-08T16:16:40-03:30
local_stamp = 2024-02-29T23:59:59.5
day = 2024-02-29
noon = 12:00:00

[owner]
name = "Ada"
tags = ["red", "green", "blue"]
mixed = [1, "two", 3.0]
point = { x = 1, y = -2 }
site.host = "example.org"

[[items]]
sku = "A-1"
qty = 3

[[items]]
sku = "B-2"
qty = 0
)";

void values() {
  const toml::table tbl = toml::parse(document);

  check(tbl["title"].value<std::string_view>() == "Inventory"sv, "a basic string");
  check(tbl["revision"].value<int64_t>() == 42, "a decimal integer");
  check(tbl["revision"].value<int>() == 42, "an integer read as int");
  check(tbl["largest"].value<int64_t>() == std::numeric_limits<int64_t>::max(),
        "INT64_MAX with digit separators");
  check(tbl["hex"].value<int64_t>() == 0xDEADBEEF, "a hexadecimal integer");
  check(tbl["octal"].value<int64_t>() == 0755, "an octal integer");
  check(tbl["binary"].value<int64_t>() == 13, "a binary integer");
  check(tbl["ratio"].value<double>() == 0.125, "an exact float");
  const double avogadro = tbl["avogadro"].value_or(0.0);
  check(std::fabs(avogadro - 6.02e23) <= 6.02e23 * 1e-15, "a float with an exponent");
  const double falling = tbl["falling"].value_or(0.0);
  check(std::isinf(falling) && falling < 0, "negative infinity");
  check(std::isnan(tbl["unknown"].value_or(0.0)), "nan");
  check(tbl["ready"].type() == toml::node_type::boolean && tbl["ready"].value_or(false),
        "a boolean");

  check(tbl["literal"].value_or(""sv) == R"(C:\temp\no\escapes)"sv,
        "a literal string keeps backslashes");
  check(tbl["escaped"].value_or(""sv) == "tab\there \xC3\xA9 \xF0\x9F\x98\x80"sv,
        "escapes, including \\u and \\U, decode to UTF-8");
  check(tbl["multiline"].value_or(""sv) == "first line\nsecond line"sv,
        "a multi-line string drops its leading newline");
  check(tbl["raw_multiline"].value_or(""sv) == R"(kept \n as written)"sv,
        "a multi-line literal string keeps escapes as written");

  const std::optional<toml::date_time> stamp = tbl["stamp"].value<toml::date_time>();
  check(stamp && stamp->date == toml::date{2001, 9, 9} &&
            stamp->time == toml::time{1, 46, 40} && stamp->offset &&
            stamp->offset->minutes == 330,
        "an offset date-time");
  const std::optional<toml::date_time> west = tbl["west"].value<toml::date_time>();
  check(west && west->offset && west->offset->minutes == -210, "a negative offset");
  const std::optional<toml::date_time> local = tbl["local_stamp"].value<toml::date_time>();
  check(local && !local->offset && local->time.nanosecond == 500000000u,
        "a local date-time with a fraction");
  check(tbl["day"].value<toml::date>() == toml::date{2024, 2, 29}, "a local date");
  check(tbl["noon"].value<toml::time>() == toml::time{12, 0, 0}, "a local time");

  const toml::array* tags = tbl["owner"]["tags"].as_array();
  check(tags && tags->size() == 3 && tags->is_homogeneous<std::string>(),
        "a homogeneous array");
  check(tbl.at_path("owner.tags[1]").value_or(""sv) == "green"sv, "at_path into an array");
  const toml::array* mixed = tbl["owner"]["mixed"].as_array();
  check(mixed && !mixed->is_homogeneous(toml::node_type::none), "a mixed array");
  check(tbl["owner"]["point"]["y"].value_or(0) == -2, "an inline table");
  check(tbl["owner"]["site"]["host"].value_or(""sv) == "example.org"sv,
        "a dotted key defines a sub-table");

  const toml::array* items = tbl["items"].as_array();
  check(items && items->size() == 2 && items->is_array_of_tables(), "an array of tables");
  check(tbl.at_path("items[0].sku").value_or(""sv) == "A-1"sv, "the first table");
  check(tbl.at_path("items[1].qty").value<int64_t>() == 0, "the second table");

  check(!tbl["absent"] && tbl["absent"].value_or(7) == 7, "a missing key");
  check(!tbl["title"].value<int64_t>(), "a string is not an integer");
  check(tbl.begin()->first == "avogadro"sv, "a table iterates its keys in order");
}

// Returns the error the document raises, or nothing.
std::optional<toml::parse_error> error_of(std::string_view doc,
                                          std::string_view path = {}) {
  try {
    (void)toml::parse(doc, path);
  } catch (const toml::parse_error& e) {
    return e;
  }
  return std::nullopt;
}

bool ends_with(std::string_view text, std::string_view suffix) {
  return text.size() >= suffix.size() &&
         text.substr(text.size() - suffix.size()) == suffix;
}

void errors() {
  std::optional<toml::parse_error> e = error_of("ok = 1\nn = 9223372036854775808\n");
  check(e && ends_with(e->description(), "'9223372036854775808' is not representable in 64 bits"),
        "an integer past INT64_MAX");
  check(e && e->source().begin.line == 2, "the overflow's line");

  e = error_of("a = 1\na = 2\n", "dup.toml");
  check(e && e->description().find("cannot redefine existing integer") !=
                 std::string_view::npos,
        "a redefined key");
  check(e && e->source().begin.line == 2, "the redefinition's line");
  check(e && e->source().path && *e->source().path == "dup.toml",
        "the error carries the source path");

  e = error_of("[t]\nx = 1\n[t]\n");
  check(e && e->source().begin.line == 3, "a redefined table");
  check(error_of("s = \"unterminated\n").has_value(), "an unterminated string");
  check(!error_of("[a.b]\n[a]\nc = 1\n"), "a super-table defined after its sub-table");

#if TOML_ENABLE_WINDOWS_COMPAT
  // The wide path converts through WideCharToMultiByte.
  try {
    (void)toml::parse("x = \n", L"wide\u00e9.toml");
    check(false, "the wide-path parse fails");
  } catch (const toml::parse_error& wide) {
    check(wide.source().path && *wide.source().path == "wide\xC3\xA9.toml",
          "a wide source path converts to UTF-8");
  }
#endif
}

void files() {
  std::filesystem::path dir;
  std::mt19937_64 random{std::random_device{}()};
  do {
    dir = std::filesystem::temp_directory_path() /
          ("tomlplusplus-test-" + std::to_string(random()));
  } while (!std::filesystem::create_directory(dir));
  const std::filesystem::path path = dir / "settings.toml";
  {
    std::ofstream out(path, std::ios::binary);
    out << "[server]\r\nport = 8080\r\nhosts = [\"a\", \"b\"]\r\n";
  }

  // parse_file takes UTF-8, which path::string() is not on Windows; the
  // copy keeps this valid whether u8string() returns string or u8string.
  const auto u8 = path.u8string();
  const std::string utf8_path(u8.begin(), u8.end());
  const toml::table tbl = toml::parse_file(utf8_path);
  check(tbl["server"]["port"].value_or(0) == 8080, "parse_file reads CRLF lines");
  check(tbl.at_path("server.hosts[1]").value_or(""sv) == "b"sv, "parse_file's arrays");
  const toml::node* server = tbl["server"].node();
  check(server && server->source().path && *server->source().path == utf8_path,
        "parsed nodes remember their file");

  std::error_code ignored;
  std::filesystem::remove_all(dir, ignored);
}

void building_and_formatting() {
  toml::table tbl{
      {"name", "cabin"},
      {"quote", "it's"},
      {"version", toml::array{3, 4, 0}},
      {"meta", toml::table{{"stable", true}}},
  };
  tbl.insert_or_assign("count", 2);
  tbl["meta"].as_table()->insert("ratio", 0.5);
  tbl["version"].as_array()->push_back(1);
  check(tbl.erase("count") == 1 && !tbl.contains("count"), "erase a key");
  check(tbl["version"].as_array()->size() == 4, "push_back onto an array");

  std::ostringstream toml_text;
  toml_text << tbl;
  const toml::table reparsed = toml::parse(toml_text.str());
  check(reparsed == tbl, "the TOML formatter's output parses back to an equal table");
  // Literal strings where nothing needs escaping, inline arrays, and a
  // header for the sub-table after the top-level keys.
  check(toml_text.str() ==
            "name = 'cabin'\n"
            "quote = \"it's\"\n"
            "version = [ 3, 4, 0, 1 ]\n"
            "\n"
            "[meta]\n"
            "ratio = 0.5\n"
            "stable = true",
        "the TOML formatter's output");

  std::ostringstream json_text;
  json_text << toml::json_formatter{toml::table{{"a", 1}, {"b", toml::array{true, "x"}}}};
  check(json_text.str() ==
            "{\n"
            "    \"a\" : 1,\n"
            "    \"b\" : [\n"
            "        true,\n"
            "        \"x\"\n"
            "    ]\n"
            "}",
        "the JSON formatter's output");
}

void versions() {
  check(TOML_LIB_MAJOR == 3 && TOML_LIB_MINOR == 4 && TOML_LIB_PATCH == 0, "toml++ 3.4.0");
  check(TOML_HEADER_ONLY == 1, "header-only mode");
  check(TOML_EXCEPTIONS == 1, "exceptions are enabled with the compiler's");
}

}  // namespace

int main() {
  try {
    versions();
    values();
    errors();
    files();
    building_and_formatting();
  } catch (const toml::parse_error& e) {
    std::fprintf(stderr, "FAIL: unexpected parse error: %.*s\n",
                 static_cast<int>(e.description().size()), e.description().data());
    ok = false;
  }
  std::printf("tomlplusplus (C++): %s\n", ok ? "ok" : "FAILED");
  return ok ? 0 : 1;
}
