// C++11: the probe compiles at the interface minimum.
#include <json/json.h>

#include <cstdint>
#include <cstdio>
#include <limits>
#include <memory>
#include <sstream>
#include <string>

namespace {

int fail(const char *what) {
  std::fprintf(stderr, "jsoncpp: %s\n", what);
  return 1;
}

bool parse(const Json::CharReaderBuilder &builder, const std::string &text,
           Json::Value *root, std::string *errors) {
  std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
  return reader->parse(text.data(), text.data() + text.size(), root, errors);
}

}  // namespace

int main() {
  Json::CharReaderBuilder builder;
  Json::Value root;
  std::string errors;

  const std::string doc =
      "{\"name\": \"caf\\u00e9 \\ud83d\\ude00\", \"count\": 3,\n"
      " \"big\": 9007199254740993, \"umax\": 18446744073709551615,\n"
      " \"neg\": -9223372036854775808, \"ratio\": 0.125,\n"
      " \"list\": [true, null, \"x\", {\"k\": []}]}";
  if (!parse(builder, doc, &root, &errors)) return fail(errors.c_str());
  if (!root.isObject() || root.size() != 7) return fail("object");
  if (root["name"].asString() != "caf\xC3\xA9 \xF0\x9F\x98\x80")
    return fail("unicode escapes");
  if (root["count"].asInt() != 3) return fail("int");
  // 2^53 + 1 is not representable as a double, so this checks int64 parsing.
  if (root["big"].asInt64() != INT64_C(9007199254740993)) return fail("int64");
  if (root["umax"].asUInt64() != std::numeric_limits<std::uint64_t>::max())
    return fail("uint64");
  if (root["neg"].asInt64() != std::numeric_limits<std::int64_t>::min())
    return fail("int64 min");
  if (root["ratio"].asDouble() != 0.125) return fail("double");
  const Json::Value &list = root["list"];
  if (!list.isArray() || list.size() != 4 || !list[0].asBool() ||
      !list[1].isNull() || list[2].asString() != "x" ||
      !list[3]["k"].isArray() || !list[3]["k"].empty())
    return fail("array");
  if (root.isMember("missing") || root.get("missing", 42).asInt() != 42)
    return fail("missing member");

  // Malformed input reports an error with its position.
  if (parse(builder, "{\"a\": [1, 2,, 3]}", &root, &errors) ||
      errors.find("Line 1, Column") == std::string::npos)
    return fail("syntax error");
  // Comments are accepted by default and rejected in strict mode.
  if (!parse(builder, "// c\n[1]", &root, &errors)) return fail("comment");
  Json::CharReaderBuilder strict;
  Json::CharReaderBuilder::strictMode(&strict.settings_);
  if (parse(strict, "// c\n[1]", &root, &errors)) return fail("strict mode");

  // Objects are ordered by key; the compact writer is deterministic.
  Json::Value out(Json::objectValue);
  out["b"] = Json::Value(Json::nullValue);
  out["a"].append(1);
  out["a"].append(2.5);
  out["a"].append("q\"\n");
  out["c"] = Json::Value::UInt64(std::numeric_limits<std::uint64_t>::max());
  Json::StreamWriterBuilder writer;
  writer["indentation"] = "";
  const std::string text = Json::writeString(writer, out);
  if (text != "{\"a\":[1,2.5,\"q\\\"\\n\"],\"b\":null,\"c\":18446744073709551615}")
    return fail(("writer: " + text).c_str());
  Json::Value back;
  if (!parse(builder, text, &back, &errors) || back != out)
    return fail("round trip");

  // Stream operators use the same reader and writer.
  std::istringstream in("{\"z\": [1.5e3]}");
  Json::Value streamed;
  in >> streamed;
  if (streamed["z"][0].asDouble() != 1500.0) return fail("operator>>");

  // Type errors throw (JSON_USE_EXCEPTION defaults to 1).
  bool threw = false;
  try {
    Json::Value("abc").asInt();
  } catch (const Json::LogicError &) {
    threw = true;
  }
  if (!threw) return fail("LogicError");

  if (std::string(JSONCPP_VERSION_STRING) != "1.9.8") return fail("version");
  return 0;
}
