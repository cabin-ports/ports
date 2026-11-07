// Consumer test for jsoncons: the core basic_json, reflection traits, and the
// jsonpointer, jsonpatch, mergepatch, jsonpath, jmespath, cbor, msgpack, ubjson,
// bson and csv extensions, through the documented headers.  The toon and
// jsonschema headers are included so that they too compile at C++14, the
// interface floor.

#include <jsoncons/json.hpp>
#include <jsoncons_ext/bson/bson.hpp>
#include <jsoncons_ext/cbor/cbor.hpp>
#include <jsoncons_ext/csv/csv.hpp>
#include <jsoncons_ext/jmespath/jmespath.hpp>
#include <jsoncons_ext/jsonpatch/jsonpatch.hpp>
#include <jsoncons_ext/jsonpath/jsonpath.hpp>
#include <jsoncons_ext/jsonpointer/jsonpointer.hpp>
#include <jsoncons_ext/jsonschema/jsonschema.hpp>
#include <jsoncons_ext/mergepatch/mergepatch.hpp>
#include <jsoncons_ext/msgpack/msgpack.hpp>
#include <jsoncons_ext/toon/toon.hpp>
#include <jsoncons_ext/ubjson/ubjson.hpp>

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#define CHECK(cond)                                                       \
  do {                                                                    \
    if (!(cond)) {                                                        \
      std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, \
                   #cond);                                                \
      return 1;                                                           \
    }                                                                     \
  } while (0)

namespace ns {
struct book {
  std::string title;
  double price;
  std::vector<std::string> tags;
};
}  // namespace ns

JSONCONS_ALL_MEMBER_TRAITS(ns::book, title, price, tags)

int main() {
  using jsoncons::json;
  CHECK(JSONCONS_VERSION_MAJOR == 1 && JSONCONS_VERSION_MINOR == 9 && JSONCONS_VERSION_PATCH == 0);

  // Core: parse, typed access, mutation, compact serialization.
  json doc = json::parse(R"({"store": {"books": [
      {"title": "A", "price": 8.5, "tags": ["x"]},
      {"title": "B", "price": 12.0, "tags": []}
  ]}, "n": 3})");
  CHECK(doc["n"].as<int>() == 3 && doc["store"]["books"].size() == 2);
  CHECK(doc["store"]["books"][1]["title"].as<std::string>() == "B");
  doc["n"] = 4;
  CHECK(doc["n"].as<int>() == 4);
  std::string compact;
  json::parse(R"({"a": [1, true, null, "s"]})").dump(compact);
  CHECK(compact == R"({"a":[1,true,null,"s"]})");
  bool threw = false;
  try {
    json::parse("{\"a\": }");
  } catch (const jsoncons::ser_error&) {
    threw = true;
  }
  CHECK(threw);

  // Reflection traits: JSON text <-> a user type.
  ns::book b = jsoncons::decode_json<ns::book>(std::string(R"({"title":"C","price":3.25,"tags":["p","q"]})"));
  CHECK(b.title == "C" && b.price == 3.25 && b.tags.size() == 2 && b.tags[1] == "q");
  std::string encoded;
  jsoncons::encode_json(b, encoded);
  CHECK(jsoncons::decode_json<ns::book>(encoded).tags[0] == "p");

  // JSON Pointer (RFC 6901).
  CHECK(jsoncons::jsonpointer::get(doc, "/store/books/0/price").as<double>() == 8.5);

  // JSON Patch (RFC 6902) and JSON Merge Patch (RFC 7386).
  json target = json::parse(R"({"a": 1, "b": [1, 2]})");
  jsoncons::jsonpatch::apply_patch(
      target, json::parse(R"([{"op": "add", "path": "/b/-", "value": 3}, {"op": "remove", "path": "/a"}])"));
  CHECK(target == json::parse(R"({"b": [1, 2, 3]})"));
  jsoncons::mergepatch::apply_merge_patch(target, json::parse(R"({"b": null, "c": "x"})"));
  CHECK(target == json::parse(R"({"c": "x"})"));

  // JSONPath and JMESPath queries.
  json cheap = jsoncons::jsonpath::json_query(doc, "$.store.books[?(@.price < 10)].title");
  CHECK(cheap == json::parse(R"(["A"])"));
  json titles = jsoncons::jmespath::search(doc, "store.books[*].title");
  CHECK(titles == json::parse(R"(["A", "B"])"));

  // CBOR (RFC 8949), MessagePack, UBJSON and BSON: exact bytes and a round trip.
  json small = json::parse("[1, -1, \"a\"]");
  std::vector<std::uint8_t> cbor_bytes;
  jsoncons::cbor::encode_cbor(small, cbor_bytes);
  const std::uint8_t cbor_expected[] = {0x83, 0x01, 0x20, 0x61, 0x61};
  CHECK(cbor_bytes == std::vector<std::uint8_t>(cbor_expected, cbor_expected + sizeof cbor_expected));
  CHECK(jsoncons::cbor::decode_cbor<json>(cbor_bytes) == small);
  std::vector<std::uint8_t> msgpack_bytes;
  jsoncons::msgpack::encode_msgpack(small, msgpack_bytes);
  const std::uint8_t msgpack_expected[] = {0x93, 0x01, 0xff, 0xa1, 0x61};
  CHECK(msgpack_bytes == std::vector<std::uint8_t>(msgpack_expected, msgpack_expected + sizeof msgpack_expected));
  CHECK(jsoncons::msgpack::decode_msgpack<json>(msgpack_bytes) == small);
  std::vector<std::uint8_t> ubjson_bytes;
  jsoncons::ubjson::encode_ubjson(small, ubjson_bytes);
  const std::uint8_t ubjson_expected[] = {'[', '#', 'U', 3, 'U', 1, 'i', 0xff, 'S', 'U', 1, 'a'};
  CHECK(ubjson_bytes == std::vector<std::uint8_t>(ubjson_expected, ubjson_expected + sizeof ubjson_expected));
  CHECK(jsoncons::ubjson::decode_ubjson<json>(ubjson_bytes) == small);
  // BSON's top level is a document: {"a": 1} as an int32 element.
  json one = json::parse(R"({"a": 1})");
  std::vector<std::uint8_t> bson_bytes;
  jsoncons::bson::encode_bson(one, bson_bytes);
  const std::uint8_t bson_expected[] = {0x0c, 0, 0, 0, 0x10, 'a', 0, 1, 0, 0, 0, 0};
  CHECK(bson_bytes == std::vector<std::uint8_t>(bson_expected, bson_expected + sizeof bson_expected));
  CHECK(jsoncons::bson::decode_bson<json>(bson_bytes) == one);

  // CSV with a header row into an array of objects.
  jsoncons::csv::csv_options csv_opts;
  csv_opts.assume_header(true);
  json rows = jsoncons::csv::decode_csv<json>(std::string("name,qty\napple,3\npear,5\n"), csv_opts);
  CHECK(rows.size() == 2 && rows[1]["name"].as<std::string>() == "pear" && rows[1]["qty"].as<int>() == 5);

  std::puts("jsoncons: ok");
  return 0;
}
