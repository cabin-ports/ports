#include "cajun/json/elements.h"
#include "cajun/json/reader.h"
#include "cajun/json/writer.h"

#include <cstdio>
#include <sstream>
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

} // namespace

int main() {
  json::Object obj;
  obj["name"] = json::String("cabin");
  obj["n"] = json::Number(3.5);
  obj["ok"] = json::Boolean(true);

  std::ostringstream os;
  json::Writer::Write(obj, os);
  CHECK(os.str() == "{\"name\":\"cabin\",\"n\":3.5,\"ok\":true}");

  std::istringstream is(os.str());
  json::Object back;
  json::Reader::Read(back, is);
  CHECK(back == obj);
  json::Object other = back;
  other["ok"] = json::Boolean(false);
  CHECK(!(other == obj));

  const json::Object& c = back;
  const json::Number& n = c["n"];
  CHECK(n.Value() == 3.5);
  const json::String& name = c["name"];
  CHECK(name.Value() == "cabin");

  bool threw = false;
  try {
    (void)c["missing"];
  } catch (const json::Exception&) {
    threw = true;
  }
  CHECK(threw);

  threw = false;
  try {
    const json::Boolean& b = c["n"];
    (void)b;
  } catch (const json::Exception&) {
    threw = true;
  }
  CHECK(threw);

  // Arrays write pretty-printed by default.
  json::Array arr;
  arr.Insert(json::Boolean(false));
  arr.Insert(json::Number(1234567.25));
  arr.Insert(json::Null());
  std::ostringstream as;
  json::Writer::Write(arr, as);
  CHECK(as.str() == "[\n\tfalse,\n\t1234567.25,\n\tnull\n]");
  std::istringstream ais(as.str());
  json::Array arrBack;
  json::Reader::Read(arrBack, ais);
  CHECK(arrBack == arr);

  // The writer escapes these and writes 2- and 3-byte UTF-8 as code points.
  std::ostringstream ws;
  json::Writer::Write(json::String("a\"\\\n\xC3\xA9\xE2\x82\xAC"), ws);
  CHECK(ws.str() == "\"a\\\"\\\\\\n\\u00e9\\u20ac\"");
  std::istringstream wis(ws.str());
  json::String sBack;
  json::Reader::Read(sBack, wis);
  CHECK(sBack.Value() == "a\"\\\n\xC3\xA9\xE2\x82\xAC");

  // Escaped code points and a surrogate pair decode to UTF-8: U+00E9, U+FFFD,
  // U+1F600.
  std::istringstream u("{\"s\":\"\\u00e9\\uFFFD\\ud83d\\ude00\"}");
  json::Object o;
  json::Reader::Read(o, u);
  const json::String& s = static_cast<const json::Object&>(o)["s"];
  CHECK(s.Value() == "\xC3\xA9\xEF\xBF\xBD\xF0\x9F\x98\x80");

  std::istringstream incomplete("[ true, ");
  json::Array array;
  threw = false;
  try {
    json::Reader::Read(array, incomplete);
  } catch (const json::Reader::ParseException&) {
    threw = true;
  }
  CHECK(threw);

  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("cajun-jsonapi ok");
  return 0;
}
