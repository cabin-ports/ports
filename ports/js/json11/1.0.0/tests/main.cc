#include <json11.hpp>

#include <cstdio>
#include <string>
#include <vector>

using json11::Json;

static int failures = 0;

#define CHECK(cond)                                                    \
  do {                                                                 \
    if (!(cond)) {                                                     \
      std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__,      \
                   __LINE__, #cond);                                   \
      ++failures;                                                      \
    }                                                                  \
  } while (0)

int main() {
  std::string err;
  Json j = Json::parse(
      R"({"k1":"v1","k2":42,"k3":["a",123,true,false,null],"k4":{"n":-1.5}})",
      err);
  CHECK(err.empty());
  CHECK(j.is_object());
  CHECK(j["k1"].is_string() && j["k1"].string_value() == "v1");
  CHECK(j["k2"].is_number() && j["k2"].int_value() == 42);
  CHECK(j["k3"].is_array() && j["k3"].array_items().size() == 5);
  CHECK(j["k3"][0].string_value() == "a");
  CHECK(j["k3"][1].number_value() == 123.0);
  CHECK(j["k3"][2].is_bool() && j["k3"][2].bool_value());
  CHECK(j["k3"][3].is_bool() && !j["k3"][3].bool_value());
  CHECK(j["k3"][4].is_null());
  CHECK(j["k4"]["n"].number_value() == -1.5);
  CHECK(j["missing"].is_null());

  // Dump formats exactly, objects ordered by key.
  Json obj = Json::object{
      {"key1", "value1"}, {"key2", false}, {"key3", Json::array{1, 2, 3}}};
  CHECK(obj.dump() == R"({"key1": "value1", "key2": false, "key3": [1, 2, 3]})");
  CHECK(Json("a\"b\n").dump() == R"("a\"b\n")");
  CHECK(Json(nullptr).dump() == "null");
  CHECK(Json(1.5).dump() == "1.5");

  // Round trip.
  Json again = Json::parse(j.dump(), err);
  CHECK(err.empty());
  CHECK(again == j);

  CHECK(obj.has_shape({{"key1", Json::STRING}, {"key2", Json::BOOL}}, err));

  // Parse errors.
  Json bad = Json::parse("[1, 2", err);
  CHECK(bad.is_null());
  CHECK(err == "unexpected end of input");
  err.clear();
  bad = Json::parse(R"({"a": tru})", err);
  CHECK(bad.is_null());
  CHECK(err == "parse error: expected true, got tru}");

  // parse_multi returns every value of a whitespace-separated sequence.
  err.clear();
  std::vector<Json> all = Json::parse_multi(R"({"a":1} [2] "x" 3)", err);
  CHECK(err.empty());
  CHECK(all.size() == 4);
  if (all.size() == 4) {
    CHECK(all[0]["a"].int_value() == 1);
    CHECK(all[1][0].int_value() == 2);
    CHECK(all[2].string_value() == "x");
    CHECK(all[3].int_value() == 3);
  }

  return failures == 0 ? 0 : 1;
}
