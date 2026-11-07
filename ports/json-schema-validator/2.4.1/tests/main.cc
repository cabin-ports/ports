// Consumer test for cabin-ports/json-schema-validator: draft-07 validation
// through the static library, its default-value patches, the built-in
// string formats, remote references through a schema loader, content
// checks, json_uri, and the embedded draft-07 meta-schema.
#include <nlohmann/json-schema.hpp>

#include <cstdio>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using nlohmann::json;
using nlohmann::json_uri;
using nlohmann::json_schema::json_validator;

namespace {

int failures = 0;

void check(bool ok, const char *what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

bool contains(const std::string &s, const char *part) {
  return s.find(part) != std::string::npos;
}

// The message of the std::exception f throws, or "" when it throws none.
template <typename F> std::string thrown(F f) {
  try {
    f();
  } catch (const std::exception &e) {
    return e.what();
  }
  return "";
}

// Records every error instead of stopping at the first, as the default
// handler does.
class collecting_handler : public nlohmann::json_schema::basic_error_handler {
public:
  std::vector<std::pair<std::string, std::string>> errors;  // pointer, message

  void error(const json::json_pointer &ptr, const json &instance,
             const std::string &message) override {
    basic_error_handler::error(ptr, instance, message);
    errors.emplace_back(ptr.to_string(), message);
  }

  // Whether an error at ptr mentions part; when none does, lists them all.
  bool has(const char *ptr, const char *part) const {
    for (const auto &e : errors)
      if (e.first == ptr && contains(e.second, part)) return true;
    for (const auto &e : errors)
      std::fprintf(stderr, "  error at '%s': %s\n", e.first.c_str(), e.second.c_str());
    return false;
  }
};

const json person_schema = R"({
  "$schema": "http://json-schema.org/draft-07/schema#",
  "type": "object",
  "properties": {
    "name": {"type": "string", "minLength": 1},
    "age": {"type": "integer", "minimum": 2, "maximum": 200},
    "tags": {
      "type": "array",
      "items": {"type": "string", "pattern": "^[a-z]+$"},
      "uniqueItems": true
    },
    "address": {
      "type": "object",
      "default": {},
      "properties": {"street": {"type": "string", "default": "Abbey Road"}}
    },
    "kind": {"enum": ["human", "robot"]}
  },
  "required": ["name", "age"],
  "additionalProperties": false
})"_json;

void test_validation() {
  json_validator validator(person_schema);

  // Every property present: valid, nothing to default.
  json full = {{"name", "Ada"},
               {"age", 36},
               {"tags", {"math", "engines"}},
               {"address", {{"street", "St James's Square"}}},
               {"kind", "human"}};
  json patch = validator.validate(full);
  check(patch.is_array() && patch.empty(), "a complete instance needs no defaults");

  // An empty address gets its street's default; a missing one gets {}
  // and, through the patch, nothing more.
  json empty_address = {{"name", "Hans"}, {"age", 69}, {"address", json::object()}};
  patch = validator.validate(empty_address);
  check(patch == R"([{"op": "add", "path": "/address/street", "value": "Abbey Road"}])"_json,
        "default patch for an empty address");
  check(empty_address.patch(patch)["address"]["street"] == "Abbey Road",
        "the default patch applies");
  json no_address = {{"name", "Hans"}, {"age", 69}};
  patch = validator.validate(no_address);
  check(patch == R"([{"op": "add", "path": "/address", "value": {}}])"_json,
        "default patch for a missing address");

  // The default handler throws std::invalid_argument at the first error,
  // naming the instance pointer.
  std::string message = thrown([&] { validator.validate(json{{"name", "Ada"}, {"age", 201}}); });
  check(contains(message, "At /age of 201") && contains(message, "maximum"),
        "default handler reports the maximum at /age");
  try {
    validator.validate(json{{"age", 36}});
    check(false, "a missing required property throws");
  } catch (const std::invalid_argument &e) {
    check(contains(e.what(), "required property 'name' not found"), "required message");
  }

  // A collecting handler sees every violation with its location.
  json bad = {{"name", ""},
              {"age", 1.5},
              {"tags", {"ok", "Bad", "ok"}},
              {"kind", "alien"},
              {"extra", true}};
  collecting_handler errors;
  validator.validate(bad, errors);
  check(static_cast<bool>(errors), "the bad instance fails");
  check(errors.has("/name", "too short"), "minLength at /name");
  check(errors.has("/age", "unexpected instance type"), "integer type at /age");
  check(errors.has("/tags/1", "regex pattern"), "pattern at /tags/1");
  check(errors.has("/tags", "unique"), "uniqueItems at /tags");
  check(errors.has("/kind", "enum"), "enum at /kind");
  check(errors.has("", "additional property 'extra'"), "additionalProperties at the root");
  check(errors.errors.size() == 6, "exactly six errors for the bad instance");

  errors.reset();
  validator.validate(json{{"name", "Ada"}, {"age", 36}}, errors);
  check(!errors && errors.errors.size() == 6, "reset clears the error flag");
}

void test_combinators() {
  json_validator validator(R"({
    "oneOf": [{"type": "integer", "multipleOf": 3}, {"type": "integer", "multipleOf": 5}],
    "not": {"const": 30},
    "if": {"minimum": 100}, "then": {"maximum": 300}
  })"_json);
  collecting_handler errors;
  validator.validate(9, errors);
  check(!errors, "9 matches exactly one branch");
  validator.validate(15, errors);
  check(errors.has("", "more than one subschema"), "15 matches both oneOf branches");
  errors = collecting_handler();
  validator.validate(7, errors);
  check(errors.has("", "no subschema has succeeded"), "7 matches no oneOf branch");
  errors = collecting_handler();
  validator.validate(30, errors);
  check(errors.has("", "required to not validate"), "not rejects 30");
  errors = collecting_handler();
  validator.validate(303, errors);
  check(errors.has("", "maximum"), "if/then applies the maximum to 303");
}

void test_formats() {
  json schema = R"({
    "type": "object",
    "properties": {
      "email": {"type": "string", "format": "email"},
      "ipv4": {"type": "string", "format": "ipv4"},
      "when": {"type": "string", "format": "date-time"},
      "id": {"type": "string", "format": "uuid"}
    }
  })"_json;

  // A format keyword without a checker is a schema error.
  check(contains(thrown([&] { json_validator v(schema); }), "format checker was not provided"),
        "format without a checker");

  json_validator validator(schema, nullptr, nlohmann::json_schema::default_string_format_check);
  json good = {{"email", "user.name+tag@example.com"},
               {"ipv4", "192.168.0.1"},
               {"when", "2026-11-07T01:23:45.5+09:00"},
               {"id", "123e4567-e89b-12d3-a456-426614174000"}};
  collecting_handler errors;
  validator.validate(good, errors);
  check(!errors, "well-formed format values");

  json bad = {{"email", "user@@example.com"},
              {"ipv4", "256.1.1.1"},
              {"when", "2026-13-07T01:23:45Z"},
              {"id", "123e4567-e89b-12d3-a456"}};
  validator.validate(bad, errors);
  check(errors.has("/email", "not a valid email"), "email format");
  check(errors.has("/ipv4", "not an IPv4"), "ipv4 format");
  check(errors.has("/when", "interval [1,12]"), "date-time month range");
  check(errors.has("/id", "uuid"), "uuid format");
  check(errors.errors.size() == 4, "exactly four format errors");

  // The checker is callable directly too.
  check(thrown([] { nlohmann::json_schema::default_string_format_check("hostname", "example.org"); })
            .empty(),
        "hostname accepted");
  check(contains(thrown([] {
                   nlohmann::json_schema::default_string_format_check("time", "25:00:00Z");
                 }),
                 "interval [0,23]"),
        "an out-of-range hour throws");
}

void test_remote_references() {
  json root = R"({
    "$id": "http://example.com/schemas/root.json",
    "type": "object",
    "properties": {"port": {"$ref": "../common/defs.json#/definitions/port"}}
  })"_json;

  check(contains(thrown([&] { json_validator v(root); }), "no loader callback given"),
        "an external reference without a loader");

  std::vector<std::string> loaded;
  auto loader = [&](const json_uri &uri, json &schema) {
    loaded.push_back(uri.location());
    if (uri.location() != "http://example.com/common/defs.json")
      throw std::invalid_argument("unknown schema " + uri.location());
    schema = R"({"definitions": {"port": {"type": "integer", "minimum": 1, "maximum": 65535}}})"_json;
  };
  json_validator validator(root, loader);
  check(loaded.size() == 1 && loaded[0] == "http://example.com/common/defs.json",
        "the loader is asked once for the resolved location");

  collecting_handler errors;
  validator.validate(json{{"port", 8080}}, errors);
  check(!errors, "a port in range");
  validator.validate(json{{"port", 70000}}, errors);
  check(errors.has("/port", "maximum"), "the referenced maximum applies");
}

void test_content() {
  json schema = R"({"type": "string", "contentMediaType": "application/json"})"_json;
  check(contains(thrown([&] { json_validator v(schema); }), "content checker was not set"),
        "contentMediaType without a checker");

  auto checker = [](const std::string &encoding, const std::string &media_type,
                    const json &instance) {
    if (!encoding.empty() || media_type != "application/json")
      throw std::invalid_argument("unexpected content keywords");
    (void)json::parse(instance.get<std::string>());
  };
  json_validator validator(schema, nullptr, nullptr, checker);
  collecting_handler errors;
  validator.validate("{\"a\": [1, 2]}", errors);
  check(!errors, "embedded JSON parses");
  validator.validate("{\"a\": ", errors);
  check(errors.has("", "content-checking failed"), "truncated embedded JSON");
}

void test_uri() {
  json_uri base("http://example.com/schemas/root.json#/definitions/a");
  check(base.scheme() == "http" && base.authority() == "example.com" &&
            base.path() == "/schemas/root.json",
        "json_uri splits a URL");
  check(base.pointer().to_string() == "/definitions/a" && base.identifier().empty(),
        "json_uri keeps a JSON pointer fragment");

  json_uri other = base.derive("../common/defs.json#port");
  check(other.location() == "http://example.com/common/defs.json", "derive resolves ..");
  check(other.identifier() == "port" && other.fragment() == "port", "plain-name fragment");
  check(base.append("b~c/d").fragment() == "/definitions/a/b~0c~1d", "append escapes a field");
  check(json_uri::escape("a/b~c") == "a~1b~0c", "escape");
  check(json_uri("urn:example:thing").location() == "urn:example:thing", "URN location");
  check(base.derive("#/x") == json_uri("http://example.com/schemas/root.json#/x"), "operator==");
}

void test_meta_schema() {
  // draft-07's meta-schema uses uri-reference, which the default checker
  // leaves to the caller.
  auto formats = [](const std::string &format, const std::string &value) {
    if (format != "uri-reference")
      nlohmann::json_schema::default_string_format_check(format, value);
  };
  json_validator meta(nlohmann::json_schema::draft7_schema_builtin, nullptr, formats);

  collecting_handler errors;
  meta.validate(person_schema, errors);
  check(!errors, "the person schema is a valid draft-07 schema");
  meta.validate(R"({"$id": "http://example.com/a.json", "$ref": "#/definitions/b",
                    "definitions": {"b": {"type": "string"}}})"_json,
                errors);
  check(!errors, "$id and $ref pass as uri-reference");
  meta.validate(R"({"type": 5, "minLength": -1, "properties": {"a": {"pattern": "("}}})"_json,
                errors);
  check(errors.has("/type", "anyOf"), "meta-schema: bad type");
  check(errors.has("/minLength", "minimum"), "meta-schema: negative minLength");
  // properties' members are additionalProperties there, reported at the
  // object with the member's error.
  check(errors.has("/properties", "additional property 'a': format-checking failed"),
        "meta-schema: bad regex");
}

}  // namespace

int main() {
  test_validation();
  test_combinators();
  test_formats();
  test_remote_references();
  test_content();
  test_uri();
  test_meta_schema();
  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("json-schema-validator consumer ok");
  return 0;
}
