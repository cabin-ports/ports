// picojson: parse a document and query it, build one and serialize it,
// read from a stream, report syntax errors, and reject arrays nested
// deeper than the parse depth limit.  C++98, as the header supports.
#include <picojson.h>

#include <cstdio>
#include <sstream>
#include <string>

namespace {

int failures = 0;

void check(bool ok, int line, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "main.cc:%d: check failed: %s\n", line, what);
    ++failures;
  }
}

#define CHECK(expr) check((expr), __LINE__, #expr)

std::string nested_arrays(int depth) {
  return std::string(depth, '[') + "0" + std::string(depth, ']');
}

}  // namespace

int main() {
  // Parse and query.
  picojson::value doc;
  const std::string err = picojson::parse(
      doc,
      "{\"name\": \"caf\\u00e9\", \"n\": 2.5, \"ok\": true, \"none\": null,"
      " \"list\": [1, -2e3, \"x\"]}");
  CHECK(err.empty());
  CHECK(doc.is<picojson::object>());
  CHECK(doc.contains("list") && !doc.contains("missing"));
  CHECK(doc.get("name").get<std::string>() == "caf\xc3\xa9");
  CHECK(doc.get("n").get<double>() == 2.5);
  CHECK(doc.get("ok").get<bool>() && doc.get("none").is<picojson::null>());
  const picojson::array& list = doc.get("list").get<picojson::array>();
  CHECK(list.size() == 3);
  CHECK(list[1].get<double>() == -2000.0 && list[2].to_str() == "x");
  CHECK(doc.get("list").get(0).serialize() == "1");

  // Build and serialize: object keys come out sorted, strings escaped.
  picojson::object obj;
  obj["b"] = picojson::value(true);
  obj["a"] = picojson::value(1.5);
  picojson::array items;
  items.push_back(picojson::value());
  items.push_back(picojson::value("tab\there"));
  obj["c"] = picojson::value(items);
  const picojson::value built(obj);
  const std::string text = built.serialize();
  CHECK(text == "{\"a\":1.5,\"b\":true,\"c\":[null,\"tab\\there\"]}");

  // A round trip gives an equal value.
  picojson::value again;
  CHECK(picojson::parse(again, text).empty() && again == built);

  // Streams.
  std::istringstream in("[10, 20, 30]");
  picojson::value from_stream;
  in >> from_stream;
  CHECK(!in.fail() && picojson::get_last_error().empty());
  CHECK(from_stream.get<picojson::array>().size() == 3);

  // Errors are reported, not thrown.
  picojson::value bad;
  const std::string syntax = picojson::parse(bad, "[1, 2");
  CHECK(syntax.find("syntax error") == 0);

  // CVE-2018-11793: arrays nested past DEFAULT_MAX_DEPTHS are rejected
  // instead of recursing.
  picojson::value deep;
  CHECK(picojson::parse(deep, nested_arrays(picojson::DEFAULT_MAX_DEPTHS))
            .empty());
  CHECK(!picojson::parse(deep, nested_arrays(picojson::DEFAULT_MAX_DEPTHS + 1))
             .empty());

  if (failures != 0) {
    return 1;
  }
  std::printf("picojson ok\n");
  return 0;
}
