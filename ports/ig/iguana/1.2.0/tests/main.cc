// C++ consumer, in C++17: iguana's JSON, XML, YAML and protobuf
// serializers on YLT_REFL-described structs, checked against JSON text and
// protobuf wire bytes written out by hand, IEEE doubles, documents the
// readers must accept or reject, and round trips; plus the jvalue DOM
// parser, enum names through enum_value (frozen maps), and prettify.
#include <iguana/json_reader.hpp>
#include <iguana/json_writer.hpp>
#include <iguana/pb_reader.hpp>
#include <iguana/pb_writer.hpp>
#include <iguana/prettify.hpp>
#include <iguana/xml_reader.hpp>
#include <iguana/xml_writer.hpp>
#include <iguana/yaml_reader.hpp>
#include <iguana/yaml_writer.hpp>

#include <array>
#include <cfloat>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <vector>

struct person {
  std::string name;
  int age;
};
YLT_REFL(person, name, age)

struct reading {
  double value;
};
YLT_REFL(reading, value)

struct wide {
  int64_t s;
  uint64_t u;
  int64_t m;
};
YLT_REFL(wide, s, u, m)

struct inner {
  int id;
  std::vector<int> v;
};
YLT_REFL(inner, id, v)

struct record {
  std::string label;
  std::optional<int> missing;
  std::optional<int> present;
  std::map<std::string, int> counts;
  std::vector<inner> items;
  bool flag;
};
YLT_REFL(record, label, missing, present, counts, items, flag)

enum class Status { STOP = 10, START };
namespace iguana {
template <>
struct enum_value<Status> {
  constexpr static std::array<int, 2> value = {10, 11};
};
}  // namespace iguana

struct job {
  Status a;
  Status b;
};
YLT_REFL(job, a, b)

struct pb_person {
  int32_t id;
  std::string name;
  int32_t age;
};
YLT_REFL(pb_person, id, name, age)

namespace {

int failures = 0;

void check(bool ok, const char* what, int line) {
  if (!ok) {
    std::printf("main.cc:%d: check failed: %s\n", line, what);
    ++failures;
  }
}
#define CHECK(cond) check((cond), #cond, __LINE__)

template <typename F>
bool throws(F f) {
  try {
    f();
  } catch (const std::exception&) {
    return true;
  }
  return false;
}

// The JSON escape for the UTF-16 code unit `hex`: a backslash, 'u' and
// four hex digits.
std::string esc(const char* hex) { return std::string("\x5C") + "u" + hex; }

bool same_bits(double a, double b) { return std::memcmp(&a, &b, sizeof a) == 0; }

double json_round_trip(double d) {
  std::string s;
  iguana::to_json(reading{d}, s);
  reading back{-1.0};
  iguana::from_json(back, s);
  return back.value;
}

double parse_value(const std::string& text) {
  reading r{-1.0};
  iguana::from_json(r, "{\"value\":" + text + "}");
  return r.value;
}

void run() {
  // JSON writer: README.md's example output, and RFC 8259 escapes, with
  // non-ASCII characters as \u escapes (a surrogate pair above U+FFFF).
  std::string json;
  iguana::to_json(person{"tom", 28}, json);
  CHECK(json == R"({"name":"tom","age":28})");
  json.clear();
  iguana::to_json(person{"q\"b\\s/\n\t\x01\xC3\xA9\xF0\x9F\x98\x80", -5}, json);
  CHECK(json == R"({"name":"q\"b\\s/\n\t)" + esc("0001") + esc("00E9") + esc("D83D") +
                    esc("DE00") + R"(","age":-5})");

  // JSON reader: any key order, whitespace, comments, \u escapes in either
  // case decoded to UTF-8 beside raw UTF-8, and unknown members skipped
  // whole, braces and quotes inside their strings included.  Text within
  // the Basic Multilingual Plane round-trips through the writer.
  person p{"", 0};
  iguana::from_json(p, R"( { "extra": {"a": [1, {"b": "} ] \" {"}], "c": null},
      /* block */ "age" : 30 , // line
      "name": "b)" + esc("00e9") + esc("4E2D") + "\xF0\x9F\x98\x80" + R"(\n\"" } )");
  CHECK(p.age == 30);
  CHECK(p.name == "b\xC3\xA9\xE4\xB8\xAD\xF0\x9F\x98\x80\n\"");
  const std::string bmp = "q\"b\\s/\n\t\x01\xC3\xA9\xE4\xB8\xAD";
  json.clear();
  iguana::to_json(person{bmp, -5}, json);
  p = person{"", 0};
  iguana::from_json(p, json);
  CHECK(p.name == bmp && p.age == -5);

  // Malformed input throws.
  CHECK(throws([] { person q; iguana::from_json(q, std::string(R"({"name":"x","age":})")); }));
  CHECK(throws([] { person q; iguana::from_json(q, std::string(R"({"name":"x)")); }));
  CHECK(throws([] { person q; iguana::from_json(q, std::string(R"({"name":"x","age":"12"})")); }));
  CHECK(throws([] {
    person q;
    iguana::from_json(q, R"({"name":")" + esc("00zz") + R"(","age":1})");
  }));

  // Doubles: decimal text parses to the nearest IEEE double, and the
  // writer's shortest form reads back to the same bits.
  CHECK(same_bits(parse_value("0.1"), 0.1));
  CHECK(same_bits(parse_value("0.30000000000000004"), 0.1 + 0.2));
  CHECK(same_bits(parse_value("1.7976931348623157e308"), DBL_MAX));
  CHECK(same_bits(parse_value("2.2250738585072014E-308"), DBL_MIN));
  CHECK(same_bits(parse_value("4.9406564584124654e-324"),
                  std::numeric_limits<double>::denorm_min()));
  CHECK(same_bits(parse_value("-0.0"), -0.0));
  CHECK(same_bits(parse_value("123456789012345678901234567890"), 1.2345678901234568e29));
  for (double d : {0.1, 1.0 / 3.0, -2.5e-310, 6.02214076e23, DBL_MAX, 5e-324, 1e21, 123.0})
    CHECK(same_bits(json_round_trip(d), d));

  // 64-bit integers, written by their own routine, beyond a double's 53
  // bits and at the type limits.
  json.clear();
  iguana::to_json(wide{-9007199254740993, std::numeric_limits<uint64_t>::max(),
                       std::numeric_limits<int64_t>::max()},
                  json);
  CHECK(json == R"({"s":-9007199254740993,"u":18446744073709551615,"m":9223372036854775807})");
  wide w{0, 0, 0};
  iguana::from_json(w, json);
  CHECK(w.s == -9007199254740993 && w.u == std::numeric_limits<uint64_t>::max() &&
        w.m == std::numeric_limits<int64_t>::max());
  json.clear();
  iguana::to_json(wide{0, 10, -10}, json);
  CHECK(json == R"({"s":0,"u":10,"m":-10})");

  // Containers, optionals, maps and nested structs, written exactly and
  // read back; an unset optional is written as null.
  record r{"x", std::nullopt, 7, {{"b", 2}, {"a", 1}}, {{1, {}}, {2, {3, 4}}}, true};
  json.clear();
  iguana::to_json(r, json);
  CHECK(json == R"({"label":"x","missing":null,"present":7,"counts":{"a":1,"b":2},)"
                R"("items":[{"id":1,"v":[]},{"id":2,"v":[3,4]}],"flag":true})");
  record r2{};
  r2.missing = 99;
  iguana::from_json(r2, json);
  CHECK(r2.label == "x" && !r2.missing && r2.present == 7 && r2.counts == r.counts &&
        r2.items.size() == 2 && r2.items[1].v == std::vector<int>({3, 4}) && r2.flag);

  // Enums as names through enum_value, and an unknown name rejected.
  json.clear();
  iguana::to_json(job{Status::START, Status::STOP}, json);
  CHECK(json == R"({"a":"START","b":"STOP"})");
  job j{Status::STOP, Status::STOP};
  iguana::from_json(j, std::string(R"({"a":"START","b":"STOP"})"));
  CHECK(j.a == Status::START && j.b == Status::STOP);
  CHECK(throws([] { job k; iguana::from_json(k, std::string(R"({"a":"PAUSE","b":"STOP"})")); }));

  // DOM parsing.
  iguana::jvalue val;
  const std::string doc = R"({"n": 3, "d": 2.5, "s": "hi", "a": [true, null], "o": {"k": false}})";
  iguana::parse(val, doc.begin(), doc.end());
  CHECK(val.is_object());
  CHECK(val.at<int>("n") == 3);
  CHECK(val.at<double>("d") == 2.5);
  CHECK(val.at<std::string>("s") == "hi");
  auto obj = val.to_object();
  CHECK(obj.size() == 5);
  CHECK(obj["a"].is_array() && obj["a"].at<bool>(0) && obj["a"].to_array()[1].is_null());
  CHECK(obj["o"].is_object() && !obj["o"].at<bool>("k"));
  std::error_code ec;
  (void)val.at<int>("s", ec);
  CHECK(static_cast<bool>(ec));

  // prettify: three-space indents, "key": value.
  CHECK(iguana::prettify(std::string(R"({"a":[1,2],"b":{}})")) ==
        "{\n   \"a\": [\n      1,\n      2\n   ],\n   \"b\": {\n      \n   }\n}");

  // XML: elements named after the members under the struct's name, the
  // README's document read back, and a round trip with escaped text.
  std::string xml;
  iguana::to_xml(person{"a<b&c", 20}, xml);
  CHECK(xml.find("<person>") != std::string::npos && xml.find("</person>") != std::string::npos);
  CHECK(xml.find("<age>20</age>") != std::string::npos);
  CHECK(xml.find("a<b") == std::string::npos);
  p = person{"", 0};
  iguana::from_xml(p, xml);
  CHECK(p.name == "a<b&c" && p.age == 20);
  p = person{"", 0};
  iguana::from_xml(p, std::string(R"(<?xml version="1.0" encoding="UTF-8"?>
<root>
  <name>buke</name>
  <age>30</age>
</root>)"));
  CHECK(p.name == "buke" && p.age == 30);

  // YAML: the README's document, and a round trip of nested data.
  p = person{"", 0};
  iguana::from_yaml(p, std::string("\nname : buke\nage : 30\n"));
  CHECK(p.name == "buke" && p.age == 30);
  std::string yaml;
  iguana::to_yaml(r, yaml);
  record r3{};
  iguana::from_yaml(r3, yaml);
  CHECK(r3.label == "x" && !r3.missing && r3.present == 7 && r3.counts == r.counts &&
        r3.items.size() == 2 && r3.items[0].id == 1 && r3.items[1].v == std::vector<int>({3, 4}) &&
        r3.flag);

  // Protobuf: fields numbered in member order; varint 1, length-delimited
  // "tom", varint 20; proto3 leaves out zero and empty fields, and a
  // negative int32 takes ten varint bytes (its 64-bit two's complement).
  std::string pb;
  iguana::to_pb(pb_person{1, "tom", 20}, pb);
  CHECK(pb == std::string("\x08\x01\x12\x03tom\x18\x14", 9));
  pb_person pp{0, "", 0};
  iguana::from_pb(pp, pb);
  CHECK(pp.id == 1 && pp.name == "tom" && pp.age == 20);
  pb.clear();
  iguana::to_pb(pb_person{-1, "", 0}, pb);
  CHECK(pb == std::string("\x08\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\x01", 11));
  pp = pb_person{5, "z", 5};
  iguana::from_pb(pp, pb);
  CHECK(pp.id == -1);
  CHECK(throws([] { pb_person q; iguana::from_pb(q, std::string("\x12\x05tom", 5)); }));
}

}  // namespace

int main() {
  try {
    run();
  } catch (const std::exception& e) {
    std::printf("unexpected exception: %s\n", e.what());
    ++failures;
  }
  std::printf("iguana: %s\n", failures == 0 ? "ok" : "FAILED");
  return failures == 0 ? 0 : 1;
}
