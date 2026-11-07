// The C++ consumer: cpptoml in C++11 - strings (basic, literal, multi-line,
// escapes), integers (separators, hex, octal, binary), floats (exponents,
// inf, nan), booleans, the four date and time types, arrays (nested and of
// tables), inline and nested tables, qualified lookups, defaults,
// range-checked integer conversions, CRLF input, parse errors with their
// line, parse_file, building a table, writing it and the parsed document
// and parsing them back, cloning, erasing, and a visitor.
#include <cpptoml.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

static bool ok = true;

static void check(bool pass, const char *what) {
    if (!pass) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ok = false;
    }
}

static bool contains(const std::string &text, const std::string &part) {
    return text.find(part) != std::string::npos;
}

static std::shared_ptr<cpptoml::table> parse(const std::string &text) {
    std::istringstream in(text);
    cpptoml::parser parser(in);
    return parser.parse();
}

template <class Error, class Function> static bool throws(Function function) {
    try {
        function();
    } catch (const Error &) {
        return true;
    }
    return false;
}

// The parse error for the text, or "" if it parses.
static std::string parse_error(const std::string &text) {
    try {
        parse(text);
    } catch (const cpptoml::parse_exception &e) {
        return e.what();
    }
    return "";
}

template <class T>
static std::string streamed(const T &value) {
    std::ostringstream out;
    out << value;
    return out.str();
}

static const char document[] =
    "# A config\n"
    "title = \"Tab\\tand \\u00e9\"\n"
    "path = 'C:\\Users\\x'\n"
    "lines = \"\"\"\nfirst\nsecond\"\"\"\n"
    "count = 1_000\n"
    "negative = -17\n"
    "hex = 0xdead_beef\n"
    "oct = 0o755\n"
    "bin = 0b1101\n"
    "ratio = 6.25e-2\n"
    "big = 1e+3\n"
    "forever = inf\n"
    "unknown = nan\n"
    "neg_forever = -inf\n"
    "off = false\n"
    "low = -300\n"
    "enabled = true\n"
    "day = 1979-05-27\n"
    "noon = 12:30:45.5\n"
    "local = 1979-05-27T07:32:00\n"
    "stamp = 1979-05-27T00:32:00.25-07:30\n"
    "east = 1979-05-27T07:32:00+05:30\n"
    "zulu = 1979-05-27T07:32:00Z\n"
    "feed = \"a\\fb\"\n"
    "raw = '''\nC:\\n\\t'''\n"
    "fold = \"\"\"a \\\n    b\"\"\"\n"
    "ints = [1, 2, 3]\n"
    "nested = [[1, 2], [\"a\", \"b\"]]\n"
    "point = { x = 1, y = 2 }\n"
    "\n"
    "[server]\n"
    "host = \"example.com\"\n"
    "ports = [80, 443]\n"
    "[server.limits]\n"
    "connections = 300\n"
    "\n"
    "[[plugin]]\n"
    "name = \"a\"\n"
    "[[plugin]]\n"
    "name = \"b\"\n";

static void reads_values(const cpptoml::table &config) {
    check(config.get_as<std::string>("title").value_or("") == "Tab\tand \xc3\xa9", "a basic string with escapes");
    check(config.get_as<std::string>("path").value_or("") == "C:\\Users\\x", "a literal string");
    check(config.get_as<std::string>("lines").value_or("") == "first\nsecond", "a multi-line string");
    check(config.get_as<int64_t>("count").value_or(0) == 1000, "an integer with separators");
    check(config.get_as<int64_t>("negative").value_or(0) == -17, "a negative integer");
    check(config.get_as<int64_t>("hex").value_or(0) == 0xdeadbeef, "a hexadecimal integer");
    check(config.get_as<int64_t>("oct").value_or(0) == 0755, "an octal integer");
    check(config.get_as<int64_t>("bin").value_or(0) == 13, "a binary integer");
    check(config.get_as<double>("ratio").value_or(0) == 0.0625, "a float with an exponent");
    check(config.get_as<double>("big").value_or(0) == 1000.0, "a float with a positive exponent");
    const double forever = config.get_as<double>("forever").value_or(0);
    const double neg_forever = config.get_as<double>("neg_forever").value_or(0);
    check(std::isinf(forever) && forever > 0 && std::isinf(neg_forever) && neg_forever < 0, "inf and -inf");
    check(config.get_as<bool>("off") && !*config.get_as<bool>("off"), "false");
    check(std::isnan(config.get_as<double>("unknown").value_or(0)), "nan");
    check(config.get_as<bool>("enabled").value_or(false), "a boolean");

    const auto day = config.get_as<cpptoml::local_date>("day");
    check(day && day->year == 1979 && day->month == 5 && day->day == 27 && streamed(*day) == "1979-05-27",
          "a local date");
    const auto noon = config.get_as<cpptoml::local_time>("noon");
    check(noon && noon->hour == 12 && noon->minute == 30 && noon->second == 45 && noon->microsecond == 500000,
          "a local time with a fraction");
    const auto local = config.get_as<cpptoml::local_datetime>("local");
    check(local && local->day == 27 && local->hour == 7 && local->minute == 32, "a local date-time");
    const auto stamp = config.get_as<cpptoml::offset_datetime>("stamp");
    check(stamp && stamp->hour == 0 && stamp->microsecond == 250000 && stamp->hour_offset == -7 &&
              stamp->minute_offset == -30,
          "an offset date-time");
    const auto east = config.get_as<cpptoml::offset_datetime>("east");
    const auto zulu = config.get_as<cpptoml::offset_datetime>("zulu");
    check(east && east->hour_offset == 5 && east->minute_offset == 30 && zulu && zulu->hour == 7 &&
              zulu->hour_offset == 0 && zulu->minute_offset == 0,
          "a positive offset and Z");
    check(config.get_as<std::string>("feed").value_or("") == "a\fb" &&
              config.get_as<std::string>("raw").value_or("") == "C:\\n\\t" &&
              config.get_as<std::string>("fold").value_or("") == "a b",
          "more escapes, a multi-line literal string and a line-ending backslash");

    std::tm moment = std::tm();
    moment.tm_year = 100;
    moment.tm_mon = 1;
    moment.tm_mday = 29;
    moment.tm_hour = 23;
    moment.tm_min = 58;
    moment.tm_sec = 59;
    const auto utc = cpptoml::offset_datetime::from_utc(moment);
    check(utc.year == 2000 && utc.month == 2 && utc.day == 29 && utc.hour == 23 && utc.minute == 58 &&
              utc.second == 59 && utc.hour_offset == 0,
          "a UTC struct tm");
}

static void reads_structure(const cpptoml::table &config) {
    const auto ints = config.get_array_of<int64_t>("ints");
    check(ints && *ints == std::vector<int64_t>({1, 2, 3}), "an array of integers");
    check(!config.get_array_of<std::string>("ints"), "an array of the wrong type");
    const auto nested = config.get_array_of<cpptoml::array>("nested");
    check(nested && nested->size() == 2 && *(*nested)[0]->get_array_of<int64_t>() == std::vector<int64_t>({1, 2}) &&
              *(*nested)[1]->get_array_of<std::string>() == std::vector<std::string>({"a", "b"}),
          "nested arrays");

    const auto point = config.get_table("point");
    check(point && point->get_as<int64_t>("x").value_or(0) == 1 && point->get_as<int64_t>("y").value_or(0) == 2,
          "an inline table");
    check(config.get_qualified_as<std::string>("server.host").value_or("") == "example.com", "a qualified key");
    check(config.get_qualified_as<int>("server.limits.connections").value_or(0) == 300, "a deeper qualified key");
    const auto limits = config.get_table_qualified("server.limits");
    check(limits && limits->contains("connections"), "a qualified table");
    const auto ports = config.get_qualified_array_of<int64_t>("server.ports");
    check(ports && *ports == std::vector<int64_t>({80, 443}), "a qualified array");
    check(config.contains("server") && config.contains_qualified("server.limits.connections") &&
              !config.contains_qualified("server.missing") && !config.contains("missing"),
          "contains");

    const auto plugins = config.get_table_array("plugin");
    std::vector<std::string> names;
    if (plugins)
        for (const auto &plugin : *plugins) names.push_back(*plugin->get_as<std::string>("name"));
    check(names == std::vector<std::string>({"a", "b"}), "an array of tables");
}

static void converts_values(const cpptoml::table &config) {
    check(!config.get_as<int64_t>("missing") && config.get_as<int64_t>("missing").value_or(5) == 5,
          "a missing key and its default");
    check(!config.get_as<std::string>("count") && !config.get_as<int64_t>("ratio") && !config.get_as<bool>("count"),
          "a value of another type");
    check(config.get_as<double>("count").value_or(0) == 1000.0, "an integer read as a double");
    check(config.get_as<int>("count").value_or(0) == 1000 && config.get_as<uint32_t>("hex").value_or(0) == 0xdeadbeef,
          "a narrower integer type");
    check(throws<std::overflow_error>([&] { config.get_as<int8_t>("count"); }) &&
              throws<std::underflow_error>([&] { config.get_as<int8_t>("low"); }) &&
              throws<std::overflow_error>([&] { config.get_as<uint8_t>("count"); }) &&
              throws<std::underflow_error>([&] { config.get_as<uint32_t>("negative"); }),
          "an integer outside the requested type");
}

static void rejects_bad_input() {
    const auto crlf = parse("a = 1\r\nb = \"x\"");
    check(crlf->get_as<int64_t>("a").value_or(0) == 1 && crlf->get_as<std::string>("b").value_or("") == "x",
          "CRLF line ends and no final line end");
    check(contains(parse_error("n = 012\n"), "leading zeros at line 1"), "a leading zero");
    check(contains(parse_error("a = 1\na = 2\n"), "at line 2"), "a duplicate key");
    check(contains(parse_error("ok = 1\ns = \"open\n"), "at line 2"), "an unterminated string");
    check(contains(parse_error("[t]\nn = 1__0\n"), "Malformed number at line 2"), "a malformed number");
    check(contains(parse_error("x = [1, \"a\"]\n"), "at line 1"), "an array of mixed types");
    check(parse_error("x = 1\n").empty(), "a good document raises nothing");
}

static void reads_files() {
    // Among the probe's own build outputs: `cabin run` keeps the caller's
    // working directory.
    const char *const dir = std::getenv("CABIN_PROFILE_DIR");
    check(dir != nullptr, "CABIN_PROFILE_DIR is set");
    if (dir == nullptr) return;
    const std::string name = std::string(dir) + "/port-probe-cpptoml.toml";
    {
        std::ofstream file(name);
        file << "answer = 42\n";
    }
    std::string error;
    std::shared_ptr<cpptoml::table> config;
    try {
        config = cpptoml::parse_file(name);
    } catch (const cpptoml::parse_exception &e) {
        error = e.what();
    }
    std::remove(name.c_str());
    check(config && config->get_as<int64_t>("answer").value_or(0) == 42, "parse_file");
    try {
        cpptoml::parse_file(name);
    } catch (const cpptoml::parse_exception &e) {
        error = e.what();
    }
    check(contains(error, "could not be opened for parsing"), "parse_file on a missing file");
}

struct Counter {
    Counter() : values(0), tables(0), arrays(0), table_arrays(0) {}
    template <class T>
    void visit(const cpptoml::value<T> &) { ++values; }
    void visit(const cpptoml::table &) { ++tables; }
    void visit(const cpptoml::array &) { ++arrays; }
    void visit(const cpptoml::table_array &) { ++table_arrays; }
    int values, tables, arrays, table_arrays;
};

static void builds_and_writes(const cpptoml::table &config) {
    auto root = cpptoml::make_table();
    root->insert("name", std::string("demo \"quoted\""));
    root->insert("answer", 42);
    root->insert("pi", 3.5);
    root->insert("third", 1.0 / 3);
    root->insert("tiny", 1e-7);
    root->insert("whole", 2.0);
    auto list = cpptoml::make_array();
    list->push_back(1);
    list->push_back(2);
    root->insert("list", list);
    auto sub = cpptoml::make_table();
    sub->insert("on", true);
    root->insert("sub", sub);
    auto items = cpptoml::make_table_array();
    auto item = cpptoml::make_table();
    item->insert("id", 7);
    items->push_back(item);
    root->insert("items", items);

    const std::string text = streamed(*root);
    check(contains(text, "name = \"demo \\\"quoted\\\"\"") && contains(text, "[sub]") && contains(text, "[[items]]"),
          "writing a table");
    std::shared_ptr<cpptoml::table> back;
    try {
        back = parse(text);
    } catch (const cpptoml::parse_exception &e) {
        std::fprintf(stderr, "FAIL: parsing the written table: %s\n", e.what());
        ok = false;
        return;
    }
    check(back->get_as<std::string>("name").value_or("") == "demo \"quoted\"" &&
              back->get_as<int64_t>("answer").value_or(0) == 42 && back->get_as<double>("pi").value_or(0) == 3.5 &&
              *back->get_array_of<int64_t>("list") == std::vector<int64_t>({1, 2}) &&
              back->get_qualified_as<bool>("sub.on").value_or(false) &&
              back->get_table_array("items")->get().size() == 1 &&
              back->get_table_array("items")->get()[0]->get_as<int64_t>("id").value_or(0) == 7,
          "a written table parses back");
    check(back->get_as<double>("third").value_or(0) == 1.0 / 3 && back->get_as<double>("tiny").value_or(0) == 1e-7 &&
              !back->get_as<int64_t>("whole") && back->get_as<double>("whole").value_or(0) == 2.0,
          "written floats parse back exactly");
    try {
        const auto again = parse(streamed(config));
        reads_values(*again);
        reads_structure(*again);
    } catch (const cpptoml::parse_exception &e) {
        std::fprintf(stderr, "FAIL: parsing the written document: %s\n", e.what());
        ok = false;
    }

    auto copy = config.clone()->as_table();
    copy->erase("title");
    copy->get_table("server")->insert("host", std::string("changed"));
    check(!copy->contains("title") && config.contains("title") &&
              copy->get_qualified_as<std::string>("server.host").value_or("") == "changed" &&
              config.get_qualified_as<std::string>("server.host").value_or("") == "example.com",
          "a clone is independent");

    Counter counter;
    config.get("count")->accept(counter);
    config.get("ints")->accept(counter);
    config.get("server")->accept(counter);
    config.get("plugin")->accept(counter);
    check(counter.values == 1 && counter.arrays == 1 && counter.tables == 1 && counter.table_arrays == 1,
          "a visitor sees each kind");
}

int main() {
    std::shared_ptr<cpptoml::table> config;
    try {
        config = parse(document);
    } catch (const cpptoml::parse_exception &e) {
        std::fprintf(stderr, "FAIL: parsing the document: %s\n", e.what());
        return 1;
    }
    try {
        reads_values(*config);
        reads_structure(*config);
        converts_values(*config);
        rejects_bad_input();
        reads_files();
        builds_and_writes(*config);
    } catch (const std::exception &e) {
        std::fprintf(stderr, "FAIL: unexpected exception: %s\n", e.what());
        return 1;
    }
    std::printf("cpptoml (C++): %s\n", ok ? "ok" : "FAILED");
    return ok ? 0 : 1;
}
