/* C consumer, in C89: a document with escapes, a surrogate pair and nested
 * objects read through the getters and dot paths; comments, malformed input
 * and the nesting limit; then a document built through the setters,
 * serialized compact and pretty, copied, compared and validated. */
#include <parson.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

static void check(int ok, const char* what, int line) {
  if (!ok) {
    fprintf(stderr, "main.c:%d: check failed: %s\n", line, what);
    ++failures;
  }
}
#define CHECK(cond) check((cond), #cond, __LINE__)

static int string_is(const char* got, const char* expected) {
  return got != NULL && strcmp(got, expected) == 0;
}

/* Parses depth nested arrays: "[[...]]". */
static int parses_nested(size_t depth) {
  char* text = (char*)malloc(2 * depth + 1);
  JSON_Value* value;
  int ok;
  if (text == NULL) {
    return -1;
  }
  memset(text, '[', depth);
  memset(text + depth, ']', depth);
  text[2 * depth] = '\0';
  value = json_parse_string(text);
  ok = value != NULL;
  json_value_free(value);
  free(text);
  return ok;
}

static void read_document(void) {
  static const char text[] =
      "{\"name\": \"parson\", \"version\": [1, 5, 3], \"ratio\": -0.25, \"big\": 1e3,"
      " \"ok\": true, \"none\": null,"
      " \"text\": \"caf\\u00e9 \\ud83d\\ude00 a\\/b\\tc\","
      " \"nested\": {\"deep\": {\"value\": 42}}}";
  JSON_Value* root = json_parse_string(text);
  JSON_Object* object = json_object(root);
  JSON_Array* version = json_object_get_array(object, "version");

  CHECK(json_value_get_type(root) == JSONObject);
  CHECK(json_object_get_count(object) == 8);
  CHECK(string_is(json_object_get_name(object, 0), "name"));
  CHECK(string_is(json_object_get_name(object, 7), "nested"));
  CHECK(string_is(json_object_get_string(object, "name"), "parson"));
  CHECK(json_array_get_count(version) == 3);
  CHECK(json_array_get_number(version, 2) == 3.0);
  CHECK(json_object_get_number(object, "ratio") == -0.25);
  CHECK(json_object_get_number(object, "big") == 1000.0);
  CHECK(json_object_get_boolean(object, "ok") == 1);
  CHECK(json_object_get_boolean(object, "name") == -1);
  CHECK(json_object_has_value_of_type(object, "none", JSONNull));
  /* \u escapes become UTF-8; the pair is one four-byte character. */
  CHECK(string_is(json_object_get_string(object, "text"),
                  "caf\xc3\xa9 \xf0\x9f\x98\x80 a/b\tc"));
  CHECK(json_object_get_string_len(object, "text") == 16);
  CHECK(json_object_dotget_number(object, "nested.deep.value") == 42.0);
  CHECK(json_object_dothas_value_of_type(object, "nested.deep", JSONObject));
  CHECK(!json_object_dothas_value(object, "nested.missing.value"));
  CHECK(json_value_get_parent(json_object_get_value(object, "nested")) == root);
  json_value_free(root);
}

static void read_edge_cases(void) {
  JSON_Value* value;
  char* serialized;

  /* An embedded NUL survives parsing and serialization. */
  value = json_parse_string("\"a\\u0000b\"");
  CHECK(json_value_get_string_len(value) == 3);
  serialized = json_serialize_to_string(value);
  CHECK(string_is(serialized, "\"a\\u0000b\""));
  json_free_serialized_string(serialized);
  json_value_free(value);

  /* Comments go, except inside strings. */
  value = json_parse_string_with_comments("/* lead */ {\"a\": 1, // note\n \"b\": \"//kept\"}");
  CHECK(json_object_get_number(json_object(value), "a") == 1.0);
  CHECK(string_is(json_object_get_string(json_object(value), "b"), "//kept"));
  json_value_free(value);
  CHECK(json_parse_string("{\"a\": 1, // note\n \"b\": 2}") == NULL);

  /* Only the first value is read. */
  value = json_parse_string("[1] trailing");
  CHECK(json_array_get_count(json_array(value)) == 1);
  json_value_free(value);

  CHECK(json_parse_string("{\"a\":}") == NULL);
  CHECK(json_parse_string("[01]") == NULL);
  CHECK(json_parse_string("[0x10]") == NULL);
  CHECK(json_parse_string("\"\\udc00\"") == NULL);
  CHECK(json_value_init_string("\xc3\x28") == NULL);

  /* A value may sit at depth 2048 (MAX_NESTING), the outermost being 0, so
   * 2049 nested arrays parse only because the innermost one is empty. */
  CHECK(parses_nested(2049) == 1);
  CHECK(parses_nested(2050) == 0);
}

static void build_document(void) {
  static const char compact[] =
      "{\"path\":\"a\\/b\",\"size\":{\"w\":0.5,\"visible\":false},\"items\":[1,\"x\\\"y\",null]}";
  static const char pretty[] =
      "{\n"
      "    \"path\": \"a\\/b\",\n"
      "    \"size\": {\n"
      "        \"w\": 0.5,\n"
      "        \"visible\": false\n"
      "    },\n"
      "    \"items\": [\n"
      "        1,\n"
      "        \"x\\\"y\",\n"
      "        null\n"
      "    ]\n"
      "}";
  JSON_Value* root = json_value_init_object();
  JSON_Object* object = json_object(root);
  JSON_Value* items = json_value_init_array();
  JSON_Value* copy;
  JSON_Value* schema;
  char* serialized;
  char buffer[128];

  CHECK(json_object_set_string(object, "path", "a/b") == JSONSuccess);
  CHECK(json_object_dotset_number(object, "size.w", 0.5) == JSONSuccess);
  CHECK(json_object_dotset_boolean(object, "size.visible", 0) == JSONSuccess);
  CHECK(json_array_append_number(json_array(items), 1) == JSONSuccess);
  CHECK(json_array_append_string(json_array(items), "x\"y") == JSONSuccess);
  CHECK(json_array_append_null(json_array(items)) == JSONSuccess);
  CHECK(json_object_set_value(object, "items", items) == JSONSuccess);

  serialized = json_serialize_to_string(root);
  CHECK(string_is(serialized, compact));
  CHECK(json_serialization_size(root) == sizeof compact);
  json_free_serialized_string(serialized);
  serialized = json_serialize_to_string_pretty(root);
  CHECK(string_is(serialized, pretty));
  json_free_serialized_string(serialized);
  CHECK(json_serialize_to_buffer(root, buffer, sizeof compact - 1) == JSONFailure);
  CHECK(json_serialize_to_buffer(root, buffer, sizeof buffer) == JSONSuccess &&
        strcmp(buffer, compact) == 0);

  copy = json_value_deep_copy(root);
  CHECK(json_value_equals(root, copy));
  CHECK(json_object_dotset_number(json_object(copy), "size.w", 0.75) == JSONSuccess);
  CHECK(!json_value_equals(root, copy));
  json_value_free(copy);

  schema = json_parse_string("{\"path\": \"\", \"size\": {\"w\": 0}, \"items\": []}");
  CHECK(json_validate(schema, root) == JSONSuccess);
  json_value_free(schema);
  schema = json_parse_string("{\"path\": 0}");
  CHECK(schema != NULL && json_validate(schema, root) == JSONFailure);
  json_value_free(schema);

  CHECK(json_object_dotremove(object, "size.visible") == JSONSuccess);
  CHECK(!json_object_dothas_value(object, "size.visible"));
  CHECK(json_object_remove(object, "missing") == JSONFailure);
  json_value_free(root);
}

int main(void) {
  read_document();
  read_edge_cases();
  build_document();
  if (failures != 0) {
    fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  puts("parson C consumer test passed");
  return 0;
}
