/* The C consumer: decoding with error positions and codes, the 64-bit
   json_int_t and its printf format, encoding (insertion order, sorted
   keys, ASCII escaping, dtoa's shortest reals), pack and unpack,
   reference counting, copies and equality, buffer, callback and
   descriptor I/O, and the parser's depth limit. */
#include <jansson.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <unistd.h>
#endif

#ifndef JANSSON_USING_CMAKE
#error "jansson_config.h is the CMake build's"
#endif

static int ok = 1;

static void check(int pass, const char *what) {
    if (!pass) {
        fprintf(stderr, "FAIL: %s\n", what);
        ok = 0;
    }
}

static int dumps_to(const json_t *json, size_t flags, const char *expected) {
    char *text = json_dumps(json, flags);
    int same = text != NULL && strcmp(text, expected) == 0;
    if (!same) {
        fprintf(stderr, "  dumped: %s\n  wanted: %s\n", text ? text : "(null)", expected);
    }
    free(text);
    return same;
}

static void version(void) {
    check(strcmp(jansson_version_str(), "2.15.1") == 0 && strcmp(JANSSON_VERSION, "2.15.1") == 0,
          "the version is 2.15.1");
    check(JANSSON_VERSION_HEX == 0x020f01, "JANSSON_VERSION_HEX");
    check(jansson_version_cmp(2, 15, 1) == 0 && jansson_version_cmp(2, 16, 0) < 0,
          "jansson_version_cmp");
}

static void decode_and_encode(void) {
    const char *text = "{\"name\": \"jansson\", \"big\": 9007199254740993,"
                       " \"low\": -9223372036854775808, \"pi\": 3.25,"
                       " \"list\": [1, true, null, \"\\u00e9\\ud83d\\ude00\"]}";
    json_error_t error;
    json_t *root = json_loads(text, 0, &error);
    json_t *list;
    char buffer[32];

    check(root != NULL && json_is_object(root) && json_object_size(root) == 5,
          "an object with five members decodes");
    if (root == NULL) {
        fprintf(stderr, "  %s\n", error.text);
        return;
    }
    check(sizeof(json_int_t) == 8, "json_int_t is 64 bits");
    /* 2^53 + 1 has no double; only an integer path keeps it. */
    check(json_integer_value(json_object_get(root, "big")) == 9007199254740993LL,
          "a 64-bit integer survives decoding");
    check(json_integer_value(json_object_get(root, "low")) == -9223372036854775807LL - 1,
          "the smallest json_int_t decodes");
    snprintf(buffer, sizeof buffer, "%" JSON_INTEGER_FORMAT,
             json_integer_value(json_object_get(root, "low")));
    check(strcmp(buffer, "-9223372036854775808") == 0, "JSON_INTEGER_FORMAT prints a json_int_t");
    check(json_real_value(json_object_get(root, "pi")) == 3.25, "a real decodes");
    list = json_object_get(root, "list");
    check(json_array_size(list) == 4 && json_is_true(json_array_get(list, 1)) &&
              json_is_null(json_array_get(list, 2)),
          "an array decodes");
    check(json_string_length(json_array_get(list, 3)) == 6 &&
              memcmp(json_string_value(json_array_get(list, 3)), "\xc3\xa9\xf0\x9f\x98\x80", 6) == 0,
          "escapes, a surrogate pair included, decode to UTF-8");

    check(dumps_to(root, JSON_COMPACT,
                   "{\"name\":\"jansson\",\"big\":9007199254740993,\"low\":-9223372036854775808,"
                   "\"pi\":3.25,\"list\":[1,true,null,\"\xc3\xa9\xf0\x9f\x98\x80\"]}"),
          "objects encode in insertion order");
    check(dumps_to(root, JSON_COMPACT | JSON_SORT_KEYS | JSON_ENSURE_ASCII,
                   "{\"big\":9007199254740993,\"list\":[1,true,null,\"\\u00E9\\uD83D\\uDE00\"],"
                   "\"low\":-9223372036854775808,\"name\":\"jansson\",\"pi\":3.25}"),
          "sorted keys and ASCII escapes");
    check(dumps_to(list, JSON_INDENT(2), "[\n  1,\n  true,\n  null,\n  \"\xc3\xa9\xf0\x9f\x98\x80\"\n]"),
          "indentation");
    json_decref(root);
}

/* dtoa's shortest round-trip digits, in jansson's format (strconv.c:69-168):
   without dtoa, 0.1 would print as 0.10000000000000001 and 1.23e47 as
   1.2299999999999999e47 (test/suites/valid/real-exponent-no-dtoa). */
static void reals(void) {
    json_t *values = json_pack("[f, f, f, f, f, f, f]", 0.1, 100.0, 1.0 / 3.0, 1.23e47, 1e300,
                               -2.5e-7, 0.5);
    check(dumps_to(values, JSON_COMPACT,
                   "[0.1,100.0,0.3333333333333333,1.23e47,1e300,-2.5e-7,0.5]"),
          "reals encode in their shortest round-trip form");
    check(dumps_to(values, JSON_COMPACT | JSON_REAL_PRECISION(3),
                   "[0.1,100.0,0.333,1.23e47,1e300,-2.5e-7,0.5]"),
          "JSON_REAL_PRECISION");
    json_decref(values);
}

static void errors(void) {
    json_error_t error;
    char deep[2 * (JSON_PARSER_MAX_DEPTH + 1) + 1];
    json_t *json;

    check(json_loads("{\"a\": [1, 2,]}", 0, &error) == NULL, "a trailing comma is rejected");
    check(json_error_code(&error) == json_error_invalid_syntax && error.line == 1 &&
              error.column == 13 && error.position == 13,
          "the error says where");
    check(strcmp(error.text, "unexpected token near ']'") == 0, "the error says what");

    check(json_loads("[\"\xff\"]", 0, &error) == NULL &&
              json_error_code(&error) == json_error_invalid_utf8,
          "invalid UTF-8 is rejected");
    check(json_loads("[1]\n[2]", 0, &error) == NULL &&
              json_error_code(&error) == json_error_end_of_input_expected && error.line == 2,
          "text after the value is rejected");
    json = json_loads("[1] [2]", JSON_DISABLE_EOF_CHECK, &error);
    check(json != NULL && json_array_size(json) == 1 && error.position == 3,
          "JSON_DISABLE_EOF_CHECK stops after the first value");
    json_decref(json);
    check(json_loads("99999999999999999999", JSON_DECODE_ANY, &error) == NULL &&
              json_error_code(&error) == json_error_numeric_overflow,
          "an integer beyond json_int_t overflows");

    memset(deep, '[', JSON_PARSER_MAX_DEPTH);
    memset(deep + JSON_PARSER_MAX_DEPTH, ']', JSON_PARSER_MAX_DEPTH);
    deep[2 * JSON_PARSER_MAX_DEPTH] = '\0';
    json = json_loads(deep, 0, &error);
    check(json != NULL, "arrays nest JSON_PARSER_MAX_DEPTH deep");
    json_decref(json);
    memset(deep, '[', JSON_PARSER_MAX_DEPTH + 1);
    memset(deep + JSON_PARSER_MAX_DEPTH + 1, ']', JSON_PARSER_MAX_DEPTH + 1);
    deep[2 * (JSON_PARSER_MAX_DEPTH + 1)] = '\0';
    check(json_loads(deep, 0, &error) == NULL &&
              json_error_code(&error) == json_error_stack_overflow,
          "one level deeper overflows");
}

static void pack_and_unpack(void) {
    json_t *packed = json_pack("{s:i, s:I, s:[s, b, n], s:f, s:s#}", "i", 42, "I",
                               (json_int_t)-5000000000LL, "a", "x", 1, "f", 2.5, "s",
                               "abcdef", 3);
    json_error_t error;
    int i = 0, b = 0;
    json_int_t big = 0;
    const char *str = NULL, *s = NULL;
    size_t length = 0;
    double f = 0;

    check(dumps_to(packed, JSON_COMPACT | JSON_SORT_KEYS,
                   "{\"I\":-5000000000,\"a\":[\"x\",true,null],\"f\":2.5,\"i\":42,\"s\":\"abc\"}"),
          "json_pack builds the value");
    check(json_unpack(packed, "{s:i, s:I, s:[s, b, n], s:f, s:s%}", "i", &i, "I", &big, "a",
                      &str, &b, "f", &f, "s", &s, &length) == 0,
          "json_unpack takes it apart");
    check(i == 42 && big == -5000000000LL && strcmp(str, "x") == 0 && b == 1 && f == 2.5 &&
              strcmp(s, "abc") == 0 && length == 3,
          "the unpacked values");
    check(json_unpack_ex(packed, &error, JSON_STRICT, "{s:i}", "i", &i) == -1 &&
              json_error_code(&error) == json_error_end_of_input_expected,
          "a strict unpack rejects unlisted keys");
    json_decref(packed);
}

static void references_and_copies(void) {
    json_t *shared = json_string("shared");
    json_t *array = json_array();
    json_t *object = json_object();
    json_t *copy;

    json_array_append(array, shared);
    json_array_append(array, shared);
    check(shared->refcount == 3, "json_array_append takes a reference");
    json_object_set_new(object, "array", array);
    json_object_set_new(object, "n", json_integer(7));
    copy = json_deep_copy(object);
    check(json_equal(copy, object), "a deep copy is equal");
    json_string_set(json_array_get(json_object_get(copy, "array"), 0), "changed");
    check(!json_equal(copy, object) && strcmp(json_string_value(shared), "shared") == 0,
          "and independent");
    json_decref(copy);
    json_decref(object);
    check(shared->refcount == 1, "releasing the container releases its references");
    json_decref(shared);
}

static size_t chunk_reader(void *buffer, size_t size, void *data) {
    const char **next = (const char **)data;
    size_t length = strlen(*next);
    if (length > size) {
        length = size;
    }
    if (length > 3) {
        length = 3; /* hand the parser three bytes at a time */
    }
    memcpy(buffer, *next, length);
    *next += length;
    return length;
}

static int append_dump(const char *buffer, size_t size, void *data) {
    strncat((char *)data, buffer, size);
    return 0;
}

static void buffers_and_callbacks(void) {
    const char *input = "{\"k\": [\"v\", 1.5]} trailing bytes";
    const char *next = "[\"chunked\", {\"x\": -1}]";
    char output[64] = "";
    char small[8];
    json_error_t error;
    json_t *json = json_loadb(input, 17, 0, &error);
    json_t *read = json_load_callback(chunk_reader, (void *)&next, 0, &error);
    json_t *joined = json_sprintf("%s-%d", "v", 7);

    check(json != NULL && json_real_value(json_array_get(json_object_get(json, "k"), 1)) == 1.5,
          "json_loadb reads only the given length");
    check(json_dump_callback(json, append_dump, output, JSON_COMPACT) == 0 &&
              strcmp(output, "{\"k\":[\"v\",1.5]}") == 0,
          "json_dump_callback");
    check(json_dumpb(json, small, sizeof small, JSON_COMPACT) == 15,
          "json_dumpb reports the size it needs");
    check(read != NULL && dumps_to(read, JSON_COMPACT, "[\"chunked\",{\"x\":-1}]"),
          "json_load_callback reads in pieces");
    check(joined != NULL && strcmp(json_string_value(joined), "v-7") == 0, "json_sprintf");
    json_decref(json);
    json_decref(read);
    json_decref(joined);
}

/* Descriptor I/O, which jansson compiles only where configure finds
   <unistd.h> (dump.c:59-65, load.c:995-1001), so not with cl; checked
   through pipe() off Windows. */
static void descriptors(void) {
#ifndef _WIN32
    int fds[2];
    json_t *json = json_pack("{s:[i, s]}", "fd", 1, "two");
    json_t *read = NULL;
    json_error_t error;

    check(pipe(fds) == 0, "a pipe");
    check(json_dumpfd(json, fds[1], JSON_COMPACT) == 0, "json_dumpfd writes");
    close(fds[1]);
    read = json_loadfd(fds[0], 0, &error);
    close(fds[0]);
    check(read != NULL && json_equal(read, json), "json_loadfd reads it back");
    json_decref(read);
    json_decref(json);
#endif
}

int main(void) {
    version();
    decode_and_encode();
    reals();
    errors();
    pack_and_unpack();
    references_and_copies();
    buffers_and_callbacks();
    descriptors();
    printf("jansson (C): %s\n", ok ? "ok" : "FAILED");
    return ok ? 0 : 1;
}
