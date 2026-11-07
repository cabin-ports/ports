// C++98, the floor of the headers.
#include "rapidjson/document.h"
#include "rapidjson/error/en.h"
#include "rapidjson/pointer.h"
#include "rapidjson/prettywriter.h"
#include "rapidjson/reader.h"
#include "rapidjson/schema.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace {

namespace rj = rapidjson;

bool ok = true;

void check(bool pass, const char* what) {
  if (!pass) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ok = false;
  }
}

std::string write(const rj::Value& value) {
  rj::StringBuffer buffer;
  rj::Writer<rj::StringBuffer> writer(buffer);
  value.Accept(writer);
  return std::string(buffer.GetString(), buffer.GetSize());
}

std::string number(unsigned n) {
  std::string digits;
  do {
    digits.insert(digits.begin(), static_cast<char>('0' + n % 10));
    n /= 10;
  } while (n != 0);
  return digits;
}

// Records each SAX event as a token and sums the integers and the
// doubles.  Returning false from an event once `stop_at` events have
// arrived ends the parse.
struct Recorder : rj::BaseReaderHandler<rj::UTF8<>, Recorder> {
  std::string events;
  int64_t sum;
  double real;
  unsigned count;
  unsigned stop_at;
  Recorder() : sum(0), real(0), count(0), stop_at(0) {}

  bool Event(const std::string& token) {
    events += token;
    ++count;
    return stop_at == 0 || count < stop_at;
  }
  bool Null() { return Event("n"); }
  bool Bool(bool b) { return Event(b ? "t" : "f"); }
  bool Int(int i) {
    sum += i;
    return Event("i");
  }
  bool Uint(unsigned u) {
    sum += u;
    return Event("u");
  }
  bool Int64(int64_t i) {
    sum += i;
    return Event("I");
  }
  bool Uint64(uint64_t u) {
    sum += static_cast<int64_t>(u);
    return Event("U");
  }
  bool Double(double d) {
    real += d;
    return Event("d");
  }
  bool String(const char* s, rj::SizeType length, bool) {
    return Event("s(" + std::string(s, length) + ")");
  }
  bool StartObject() { return Event("{"); }
  bool Key(const char* s, rj::SizeType length, bool) {
    return Event("k(" + std::string(s, length) + ")");
  }
  bool EndObject(rj::SizeType members) { return Event("}" + number(members)); }
  bool StartArray() { return Event("["); }
  bool EndArray(rj::SizeType elements) { return Event("]" + number(elements)); }
};

rj::ParseErrorCode parse_error(const char* json, size_t* offset) {
  rj::Document d;
  d.Parse(json);
  *offset = d.GetErrorOffset();
  return d.GetParseError();
}

bool valid_utf8(const char* json) {
  rj::Document d;
  d.Parse<rj::kParseValidateEncodingFlag>(json);
  return !d.HasParseError();
}

bool conforms(const rj::SchemaDocument& schema, const char* json) {
  rj::Document instance;
  instance.Parse(json);
  rj::SchemaValidator validator(schema);
  instance.Accept(validator);
  return !instance.HasParseError() && validator.IsValid();
}

}  // namespace

int main() {
  const uint64_t uint64_max = RAPIDJSON_UINT64_C2(0xFFFFFFFF, 0xFFFFFFFF);
  const int64_t int64_min = -static_cast<int64_t>(RAPIDJSON_UINT64_C2(0x7FFFFFFF, 0xFFFFFFFF)) - 1;

  // Parse and query.
  rj::Document d;
  d.Parse(
      "{\"name\":\"cabin\",\"version\":[1,1,0],\"pi\":3.25,\"tiny\":1e-7,"
      "\"max\":18446744073709551615,\"min\":-9223372036854775808,\"big\":4294967296,"
      "\"neg\":-42,\"ok\":true,\"none\":null,"
      "\"esc\":\"q\\\"b\\\\s\\/\\u00e9\\ud83d\\ude00\\n\"}");
  check(!d.HasParseError() && d.IsObject() && d.MemberCount() == 11, "the document parses");
  check(d["name"].IsString() && d["name"].GetStringLength() == 5 &&
            std::strcmp(d["name"].GetString(), "cabin") == 0,
        "string member");
  const rj::Value& version = d["version"];
  check(version.IsArray() && version.Size() == 3 && version[0].GetInt() == 1 &&
            version[1].GetInt() == 1 && version[2].IsInt() && version[2].GetInt() == 0,
        "array member");
  check(d["pi"].IsDouble() && d["pi"].GetDouble() == 3.25 && d["tiny"].GetDouble() == 1e-7,
        "doubles");
  check(d["max"].IsUint64() && !d["max"].IsInt64() && d["max"].GetUint64() == uint64_max,
        "uint64 maximum");
  check(d["min"].IsInt64() && !d["min"].IsUint64() && d["min"].GetInt64() == int64_min,
        "int64 minimum");
  check(d["big"].IsInt64() && !d["big"].IsInt() && !d["big"].IsUint() &&
            d["big"].GetInt64() == static_cast<int64_t>(RAPIDJSON_UINT64_C2(1, 0)),
        "a number past 32 bits");
  check(d["neg"].IsInt() && d["neg"].GetInt() == -42 && !d["neg"].IsUint(), "negative int");
  check(d["ok"].IsTrue() && d["none"].IsNull() && !d.HasMember("missing") &&
            d.FindMember("missing") == d.MemberEnd(),
        "literals and lookups");
  const char decoded[] = "q\"b\\s/\xC3\xA9\xF0\x9F\x98\x80\n";
  check(d["esc"].GetStringLength() == sizeof decoded - 1 &&
            std::memcmp(d["esc"].GetString(), decoded, sizeof decoded - 1) == 0,
        "escapes decode to UTF-8, the surrogate pair to one code point");

  // Full-precision parsing rounds to the nearest double: the largest
  // subnormal, DBL_MAX, the halfway points 1 + 2^-53 and 1 + 3 * 2^-53,
  // whose ties go to the even neighbour (below, then above), and just
  // above the first.
  rj::Document precise;
  precise.Parse<rj::kParseFullPrecisionFlag>(
      "[2.2250738585072011e-308,1.7976931348623157e308,"
      "1.00000000000000011102230246251565404236316680908203125,"
      "1.00000000000000033306690738754696212708950042724609375,"
      "1.00000000000000011102230246251565404236316680908203126]");
  check(!precise.HasParseError() && precise.Size() == 5 &&
            precise[0].GetDouble() == 2.2250738585072011e-308 &&
            precise[1].GetDouble() == 1.7976931348623157e308 && precise[2].GetDouble() == 1.0 &&
            precise[3].GetDouble() == 1.0000000000000004 &&
            precise[4].GetDouble() == 1.0000000000000002,
        "full-precision doubles");

  // Build and edit a document, then write it.
  rj::Document out(rj::kObjectType);
  rj::Document::AllocatorType& a = out.GetAllocator();
  out.AddMember("name", "cabin", a);
  rj::Value list(rj::kArrayType);
  rj::Value null_value;
  list.PushBack(1, a).PushBack(-2, a).PushBack(0.5, a).PushBack(true, a).PushBack(null_value, a);
  out.AddMember("list", list, a);
  char scratch[] = "copied";
  rj::Value copied;
  copied.SetString(scratch, 6, a);
  scratch[0] = 'X';
  out.AddMember("copy", copied, a);
  out.AddMember("text", "\xC3\xA9\"\x1F/", a);
  out.AddMember("nums", rj::Value(rj::kArrayType).Move(), a);
  out["nums"].PushBack(0.1, a).PushBack(3.0, a).PushBack(1e21, a).PushBack(-1e-7, a);
  out.AddMember("gone", 1, a);
  check(out.RemoveMember("gone") && !out.HasMember("gone"), "a removed member");
  check(list.IsNull(), "AddMember moves the value out");
  const std::string text = write(out);
  check(text ==
            "{\"name\":\"cabin\",\"list\":[1,-2,0.5,true,null],\"copy\":\"copied\","
            "\"text\":\"\xC3\xA9\\\"\\u001F/\",\"nums\":[0.1,3.0,1e21,-1e-7]}",
        "Writer output");
  check(write(rj::Value(1.7976931348623157e308)) == "1.7976931348623157e308" &&
            write(rj::Value(5e-324)) == "5e-324",
        "shortest round-trip output at DBL_MAX and the smallest subnormal");

  // Round trip: the output parses back into an equal document.
  rj::Document back;
  back.Parse(text.c_str());
  check(!back.HasParseError() && back == out && write(back) == text, "round trip");
  back["list"][1].SetInt(2);
  check(back != out, "an edited copy differs");

  // Deep copy into another allocator.
  rj::Document clone;
  clone.CopyFrom(out, clone.GetAllocator());
  out["list"].Clear();
  check(clone["list"].Size() == 5 && out["list"].Empty(), "CopyFrom copies deeply");

  // PrettyWriter.
  rj::Document small;
  small.Parse("{\"a\":[1,2],\"b\":{}}");
  rj::StringBuffer pretty_buffer;
  rj::PrettyWriter<rj::StringBuffer> pretty(pretty_buffer);
  pretty.SetIndent(' ', 2);
  small.Accept(pretty);
  check(std::strcmp(pretty_buffer.GetString(), "{\n  \"a\": [\n    1,\n    2\n  ],\n  \"b\": {}\n}") ==
            0,
        "PrettyWriter output");

  // In-situ parsing decodes strings into the input buffer.
  char insitu[] = "{\"k\":\"v\\u0041\"}";
  rj::Document in_place;
  in_place.ParseInsitu(insitu);
  const char* k = in_place["k"].GetString();
  check(!in_place.HasParseError() && std::strcmp(k, "vA") == 0 && k > insitu &&
            k < insitu + sizeof insitu,
        "in-situ parse");

  // Parse errors, their offsets and messages.
  size_t offset = 0;
  check(parse_error("{\"a\":1,}", &offset) == rj::kParseErrorObjectMissName && offset == 7,
        "missing member name");
  check(std::strcmp(rj::GetParseError_En(rj::kParseErrorObjectMissName),
                    "Missing a name for object member.") == 0,
        "English message");
  check(parse_error("[1,2", &offset) == rj::kParseErrorArrayMissCommaOrSquareBracket && offset == 4,
        "unterminated array");
  check(parse_error("{} x", &offset) == rj::kParseErrorDocumentRootNotSingular && offset == 3,
        "trailing content");
  check(parse_error("\"\\ud800\"", &offset) == rj::kParseErrorStringUnicodeSurrogateInvalid,
        "lone surrogate escape");
  check(parse_error("", &offset) == rj::kParseErrorDocumentEmpty, "empty document");
  rj::Document stop_when_done;
  stop_when_done.Parse<rj::kParseStopWhenDoneFlag>("{} x");
  check(!stop_when_done.HasParseError() && stop_when_done.ObjectEmpty(),
        "kParseStopWhenDoneFlag ignores trailing content");

  // UTF-8 validation.
  check(valid_utf8("\"a\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80\""), "valid UTF-8");
  check(!valid_utf8("\"\xC0\x80\"") && !valid_utf8("\"\xE0\x80\x80\"") &&
            !valid_utf8("\"\xED\xA0\x80\"") && !valid_utf8("\"\xF4\x90\x80\x80\"") &&
            !valid_utf8("\"\xF0\x80\x80\x80\"") && !valid_utf8("\"\x80\"") &&
            !valid_utf8("\"\xC3\""),
        "overlong, surrogate, out-of-range and truncated UTF-8 are rejected");

  // Transcoding: UTF-8 input decoded into a UTF-16 document.
  rj::GenericDocument<rj::UTF16<> > wide;
  wide.Parse<rj::kParseDefaultFlags, rj::UTF8<> >("\"\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80\"");
  check(!wide.HasParseError() && wide.GetStringLength() == 4 && wide.GetString()[0] == 0xE9 &&
            wide.GetString()[1] == 0x20AC && wide.GetString()[2] == 0xD83D &&
            wide.GetString()[3] == 0xDE00,
        "UTF-8 transcodes to UTF-16 code units");

  // JSON Pointer.
  check(rj::Pointer("/version/2").Get(d) == &version[2] && rj::Pointer("/nope").Get(d) == 0,
        "Pointer lookup");
  rj::SetValueByPointer(d, "/added/x~1y", 9);
  check(d["added"].HasMember("x/y") && d["added"]["x/y"].GetInt() == 9 &&
            rj::GetValueByPointer(d, "/added/x~1y")->GetInt() == 9,
        "Pointer creates the path, ~1 escaping /");

  // JSON Schema validation, the pattern run by the regex engine.
  rj::Document schema_json;
  schema_json.Parse(
      "{\"type\":\"object\",\"required\":[\"id\"],\"properties\":{"
      "\"id\":{\"type\":\"string\",\"pattern\":\"^[a-z]+-[0-9]{2}$\"},"
      "\"n\":{\"type\":\"integer\",\"minimum\":0}}}");
  rj::SchemaDocument schema(schema_json);
  check(conforms(schema, "{\"id\":\"zebra-09\",\"n\":0}"), "a conforming instance");
  check(!conforms(schema, "{\"id\":\"Cabin-07\"}") && !conforms(schema, "{\"id\":\"cabin-7\"}") &&
            !conforms(schema, "{\"id\":\"cabin-070\"}") && !conforms(schema, "{\"id\":\"-07\"}") &&
            !conforms(schema, "{\"n\":3}") &&
            !conforms(schema, "{\"id\":\"cabin-07\",\"n\":-1}"),
        "pattern, required and minimum violations are rejected");

  // SAX: a handler and a Writer driven by Reader.
  const char sax_json[] =
      "{\"id\":7,\"tags\":[\"a\",\"b\"],\"deltas\":[-3,4294967296,-4294967296],"
      "\"ratio\":0.5,\"live\":true,\"note\":null}";
  rj::Reader reader;
  Recorder recorder;
  rj::StringStream all(sax_json);
  check(!reader.Parse(all, recorder).IsError() &&
            recorder.events ==
                "{k(id)uk(tags)[s(a)s(b)]2k(deltas)[iUI]3k(ratio)dk(live)tk(note)n}6" &&
            recorder.sum == 4 && recorder.real == 0.5,
        "SAX events");
  Recorder stopping;
  stopping.stop_at = 3;
  rj::StringStream again(sax_json);
  rj::ParseResult stopped = reader.Parse(again, stopping);
  check(stopped.Code() == rj::kParseErrorTermination && stopping.events == "{k(id)u",
        "a handler returning false terminates the parse");
  rj::StringBuffer minified;
  rj::Writer<rj::StringBuffer> minifier(minified);
  rj::StringStream spaced("{ \"a\" : [ 1 , 2.5 ] ,\n \"b\" : \"c\" }");
  check(!reader.Parse(spaced, minifier).IsError() &&
            std::strcmp(minified.GetString(), "{\"a\":[1,2.5],\"b\":\"c\"}") == 0,
        "Reader into Writer minifies");

  std::printf("rapidjson (C++): %s\n", ok ? "ok" : "FAILED");
  return ok ? 0 : 1;
}
