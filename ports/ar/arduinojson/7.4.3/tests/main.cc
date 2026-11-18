#include <ArduinoJson.h>

#include <cmath>
#include <cstdio>
#include <string>

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

// README.md:106's deserialization example, with numbers exact in binary.
void check_deserialize() {
  JsonDocument doc;
  DeserializationError err = deserializeJson(
      doc, "{\"sensor\":\"gps\",\"time\":1351824120,\"data\":[48.75,2.5]}");
  CHECK(err == DeserializationError::Ok);
  CHECK(doc["sensor"].as<std::string>() == "gps");
  CHECK(doc["time"].as<long>() == 1351824120L);
  CHECK(doc["data"].size() == 2);
  CHECK(doc["data"][0].as<double>() == 48.75);
  CHECK(doc["data"][1].as<double>() == 2.5);
  CHECK(doc["missing"].isNull());

  JsonDocument bad;
  err = deserializeJson(bad, "{\"a\":");
  CHECK(err == DeserializationError::IncompleteInput);
  CHECK(std::string(err.c_str()) == "IncompleteInput");
  CHECK(deserializeJson(bad, "[1,]") == DeserializationError::InvalidInput);
}

void check_serialize_and_msgpack() {
  JsonDocument out;
  out["hello"] = "world";
  out["n"] = 42;
  std::string json;
  serializeJson(out, json);
  CHECK(json == "{\"hello\":\"world\",\"n\":42}");
  CHECK(measureJson(out) == 24);

  // fixmap(2), fixstr "hello", fixstr "world", fixstr "n", fixint 42.
  std::string packed;
  serializeMsgPack(out, packed);
  CHECK(measureMsgPack(out) == 16);
  CHECK(packed == std::string("\x82\xa5hello\xa5world\xa1n\x2a", 16));

  JsonDocument back;
  CHECK(deserializeMsgPack(back, packed) == DeserializationError::Ok);
  CHECK(back["hello"].as<std::string>() == "world");
  CHECK(back["n"].as<int>() == 42);
}

// The 7.4.3 fix (issue #2220): as<double>() on a string whose exponent runs
// past the powers-of-ten table yields NaN, as upstream's
// extras/tests/Numbers/parseDouble.cpp expects.
void check_long_number_string() {
  JsonDocument doc;
  doc["big"] = "1" + std::string(549, '0');
  CHECK(std::isnan(doc["big"].as<double>()));
  doc["small"] = "2.5";
  CHECK(doc["small"].as<double>() == 2.5);
}

// Double-precision parsing, a negative integer, exponents up to the 1e256
// table entry, escapes in both directions, and an empty array.
void check_numbers_and_escapes() {
  JsonDocument doc;
  CHECK(deserializeJson(doc, "[48.756080,-7,1e300,\"a\\nb\",[]]") ==
        DeserializationError::Ok);
  CHECK(std::fabs(doc[0].as<double>() - 48.756080) < 1e-9);
  CHECK(doc[1].as<long>() == -7L);
  double big = doc[2].as<double>();
  CHECK(big > 0.999e300 && big < 1.001e300);
  CHECK(doc[3].as<std::string>() == "a\nb");
  CHECK(doc[4].is<JsonArray>() && doc[4].size() == 0);

  JsonDocument quoted;
  quoted["q"] = "x\"y";
  std::string out;
  serializeJson(quoted, out);
  CHECK(out == "{\"q\":\"x\\\"y\"}");

  JsonDocument f;
  f["v"] = "1e35";
  double v = f["v"].as<double>();
  CHECK(v > 0.999e35 && v < 1.001e35);
}

} // namespace

int main() {
  CHECK(std::string(ARDUINOJSON_VERSION) == "7.4.3");
  check_deserialize();
  check_serialize_and_msgpack();
  check_long_number_string();
  check_numbers_and_escapes();
  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("arduinojson ok");
  return 0;
}
