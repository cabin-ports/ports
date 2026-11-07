/* yyjson from C: read a document and check every kind of value, write it
   back minified, ASCII-escaped and pretty, print numbers in the
   documented format, report errors at their positions, accept the
   non-standard extensions only when asked, build mutable documents,
   apply JSON Pointer, Patch and Merge Patch, read incrementally, and
   allocate from a fixed pool. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <yyjson.h>

static int failures = 0;

static void check(int ok, int line, const char *what) {
  if (!ok) {
    fprintf(stderr, "main.c:%d: check failed: %s\n", line, what);
    ++failures;
  }
}

#define CHECK(cond) check((cond) != 0, __LINE__, #cond)

static const char kDoc[] =
    "{\"name\":\"caf\\u00e9 \\ud83d\\ude00\",\"int\":-42,"
    "\"uint\":18446744073709551615,\"real\":0.5,\"list\":[1,2,3],"
    "\"nested\":{\"ok\":true,\"none\":null}}";

/* kDoc's "name", unescaped: U+00E9 and U+1F600 as UTF-8. */
static const char kName[] = "caf\xc3\xa9 \xf0\x9f\x98\x80";

static yyjson_doc *parse(const char *json, yyjson_read_flag flg) {
  return yyjson_read(json, strlen(json), flg);
}

/* Whether yyjson_write(doc, flg) produces exactly expected. */
static int writes(const yyjson_doc *doc, yyjson_write_flag flg,
                  const char *expected) {
  size_t len = 0;
  char *json = yyjson_write(doc, flg, &len);
  int ok = json != NULL && len == strlen(expected) &&
           memcmp(json, expected, len) == 0;
  if (!ok) {
    fprintf(stderr, "wrote: %s\n", json != NULL ? json : "(null)");
  }
  free(json);
  return ok;
}

static int mut_writes(const yyjson_mut_doc *doc, const char *expected) {
  size_t len = 0;
  char *json = yyjson_mut_write(doc, YYJSON_WRITE_NOFLAG, &len);
  int ok = json != NULL && len == strlen(expected) &&
           memcmp(json, expected, len) == 0;
  if (!ok) {
    fprintf(stderr, "wrote: %s\n", json != NULL ? json : "(null)");
  }
  free(json);
  return ok;
}

static void check_read_write(void) {
  yyjson_doc *doc = parse(kDoc, YYJSON_READ_NOFLAG);
  yyjson_doc *pretty;
  yyjson_val *root;
  yyjson_val *val;
  yyjson_val *item;
  size_t idx;
  size_t max;
  int64_t sum = 0;

  CHECK(doc != NULL);
  if (doc == NULL) {
    return;
  }
  root = yyjson_doc_get_root(doc);
  CHECK(yyjson_is_obj(root) && yyjson_obj_size(root) == 6);
  val = yyjson_obj_get(root, "name");
  CHECK(yyjson_is_str(val) && yyjson_equals_str(val, kName));
  CHECK(yyjson_get_len(val) == 10);
  val = yyjson_obj_get(root, "int");
  CHECK(yyjson_is_sint(val) && yyjson_get_sint(val) == -42);
  val = yyjson_obj_get(root, "uint");
  CHECK(yyjson_is_uint(val) && yyjson_get_uint(val) == ~(uint64_t)0);
  val = yyjson_obj_get(root, "real");
  CHECK(yyjson_is_real(val) && yyjson_get_real(val) == 0.5);
  val = yyjson_obj_get(root, "list");
  CHECK(yyjson_is_arr(val) && yyjson_arr_size(val) == 3);
  yyjson_arr_foreach(val, idx, max, item) {
    sum += yyjson_get_int(item);
  }
  CHECK(sum == 6);
  CHECK(yyjson_obj_get(root, "missing") == NULL);

  /* JSON Pointer (RFC 6901). */
  CHECK(yyjson_get_bool(yyjson_doc_ptr_get(doc, "/nested/ok")));
  CHECK(yyjson_is_null(yyjson_doc_ptr_get(doc, "/nested/none")));
  CHECK(yyjson_get_int(yyjson_doc_ptr_get(doc, "/list/2")) == 3);
  CHECK(yyjson_doc_ptr_get(doc, "/list/3") == NULL);

  /* By default the writer emits UTF-8 as is; ESCAPE_UNICODE escapes it
     with upper-case hex digits. */
  CHECK(writes(doc, YYJSON_WRITE_NOFLAG,
               "{\"name\":\"caf\xc3\xa9 \xf0\x9f\x98\x80\",\"int\":-42,"
               "\"uint\":18446744073709551615,\"real\":0.5,"
               "\"list\":[1,2,3],\"nested\":{\"ok\":true,\"none\":null}}"));
  CHECK(writes(doc, YYJSON_WRITE_ESCAPE_UNICODE,
               "{\"name\":\"caf\\u00E9 \\uD83D\\uDE00\",\"int\":-42,"
               "\"uint\":18446744073709551615,\"real\":0.5,"
               "\"list\":[1,2,3],\"nested\":{\"ok\":true,\"none\":null}}"));
  yyjson_doc_free(doc);

  pretty = parse("{\"a\":[1,{}],\"b\":\"x\"}", YYJSON_READ_NOFLAG);
  CHECK(writes(pretty, YYJSON_WRITE_PRETTY,
               "{\n"
               "    \"a\": [\n"
               "        1,\n"
               "        {}\n"
               "    ],\n"
               "    \"b\": \"x\"\n"
               "}"));
  CHECK(writes(pretty, YYJSON_WRITE_PRETTY_TWO_SPACES,
               "{\n"
               "  \"a\": [\n"
               "    1,\n"
               "    {}\n"
               "  ],\n"
               "  \"b\": \"x\"\n"
               "}"));
  yyjson_doc_free(pretty);
}

static void check_numbers(void) {
  /* Integers keep their kind; overflowing ones become doubles.  Doubles
     print as the shortest round-trip digits in ECMAScript's layout, with
     no '+' in the exponent, the sign of -0.0 kept, and ".0" on integral
     values (write_f64_raw's contract, src/yyjson.c:8018-8024). */
  yyjson_doc *doc = parse(
      "[100,1e2,0.1,1e300,5e-324,0.000001,1e-7,-0.0,"
      "1.7976931348623157e308,1e20,1e21,18446744073709551615,"
      "-9223372036854775808,-9223372036854775809]",
      YYJSON_READ_NOFLAG);
  CHECK(doc != NULL);
  CHECK(writes(doc, YYJSON_WRITE_NOFLAG,
               "[100,100.0,0.1,1e300,5e-324,0.000001,1e-7,-0.0,"
               "1.7976931348623157e308,100000000000000000000.0,1e21,"
               "18446744073709551615,-9223372036854775808,"
               "-9223372036854776000.0]"));
  yyjson_doc_free(doc);
}

static void check_errors(void) {
  /* Codes and positions as upstream's test/test_err_code.c expects. */
  static const struct {
    const char *json;
    yyjson_read_code code;
    size_t pos;
  } cases[] = {
      {"[abc]", 6, 1},             /* UNEXPECTED_CHARACTER */
      {"[1,]", 7, 2},              /* JSON_STRUCTURE */
      {"\"\\uD800\"", 10, 1},      /* INVALID_STRING: lone surrogate */
      {"\"\xff\"", 10, 1},         /* INVALID_STRING: not UTF-8 */
  };
  yyjson_read_err err;
  size_t i;

  for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
    memset(&err, 0, sizeof(err));
    CHECK(yyjson_read_opts((char *)cases[i].json, strlen(cases[i].json), 0,
                           NULL, &err) == NULL);
    CHECK(err.code == cases[i].code && err.pos == cases[i].pos);
  }
  CHECK(YYJSON_READ_ERROR_UNEXPECTED_CHARACTER == 6 &&
        YYJSON_READ_ERROR_JSON_STRUCTURE == 7 &&
        YYJSON_READ_ERROR_INVALID_STRING == 10);
  memset(&err, 0, sizeof(err));
  CHECK(yyjson_read_opts((char *)"[1,2", 4, 0, NULL, &err) == NULL);
  CHECK(err.code == YYJSON_READ_ERROR_UNEXPECTED_END);
}

static void check_non_standard(void) {
  const char *commented = "[1,/* two */2,]";
  yyjson_doc *doc;
  yyjson_write_err err;
  char *json;

  CHECK(parse(commented, YYJSON_READ_NOFLAG) == NULL);
  doc = parse(commented,
              YYJSON_READ_ALLOW_COMMENTS | YYJSON_READ_ALLOW_TRAILING_COMMAS);
  CHECK(doc != NULL && yyjson_arr_size(yyjson_doc_get_root(doc)) == 2);
  yyjson_doc_free(doc);

  CHECK(parse("[NaN,-Infinity]", YYJSON_READ_NOFLAG) == NULL);
  doc = parse("[NaN,-Infinity]", YYJSON_READ_ALLOW_INF_AND_NAN);
  CHECK(doc != NULL);
  /* Standard JSON has no such numbers: the writer refuses them unless
     told how to spell them. */
  json = yyjson_write_opts(doc, YYJSON_WRITE_NOFLAG, NULL, NULL, &err);
  CHECK(json == NULL && err.code == YYJSON_WRITE_ERROR_NAN_OR_INF);
  CHECK(writes(doc, YYJSON_WRITE_INF_AND_NAN_AS_NULL, "[null,null]"));
  CHECK(writes(doc, YYJSON_WRITE_ALLOW_INF_AND_NAN, "[NaN,-Infinity]"));
  yyjson_doc_free(doc);
}

static void check_mutable(void) {
  yyjson_mut_doc *mdoc = yyjson_mut_doc_new(NULL);
  yyjson_mut_val *root = yyjson_mut_obj(mdoc);
  yyjson_mut_val *list;
  yyjson_doc *doc;
  yyjson_mut_doc *copy;

  yyjson_mut_doc_set_root(mdoc, root);
  yyjson_mut_obj_add_str(mdoc, root, "name", "yyjson");
  yyjson_mut_obj_add_int(mdoc, root, "n", -1);
  yyjson_mut_obj_add_bool(mdoc, root, "ok", true);
  list = yyjson_mut_obj_add_arr(mdoc, root, "list");
  yyjson_mut_arr_add_uint(mdoc, list, 7);
  yyjson_mut_arr_add_null(mdoc, list);
  /* Members are written in insertion order. */
  CHECK(mut_writes(mdoc,
                   "{\"name\":\"yyjson\",\"n\":-1,\"ok\":true,\"list\":[7,null]}"));
  yyjson_mut_doc_free(mdoc);

  /* A mutable copy of a read document is independent of it. */
  doc = parse(kDoc, YYJSON_READ_NOFLAG);
  copy = yyjson_doc_mut_copy(doc, NULL);
  yyjson_doc_free(doc);
  root = yyjson_mut_doc_get_root(copy);
  CHECK(yyjson_mut_obj_remove_key(root, "nested") != NULL);
  CHECK(yyjson_mut_obj_remove_key(root, "name") != NULL);
  CHECK(mut_writes(copy,
                   "{\"int\":-42,\"uint\":18446744073709551615,\"real\":0.5,"
                   "\"list\":[1,2,3]}"));
  yyjson_mut_doc_free(copy);
}

/* Whether result equals the JSON text expected, member order aside. */
static int equals_json(yyjson_mut_doc *mdoc, yyjson_mut_val *result,
                       const char *expected) {
  yyjson_doc *doc = parse(expected, YYJSON_READ_NOFLAG);
  yyjson_mut_val *want = yyjson_val_mut_copy(mdoc, yyjson_doc_get_root(doc));
  int ok = result != NULL && yyjson_mut_equals(result, want);
  yyjson_doc_free(doc);
  return ok;
}

static void check_patches(void) {
  /* The merge example of RFC 7386, section 3. */
  yyjson_doc *target = parse(
      "{\"title\":\"Goodbye!\",\"author\":{\"givenName\":\"John\","
      "\"familyName\":\"Doe\"},\"tags\":[\"example\",\"sample\"],"
      "\"content\":\"This will be unchanged\"}",
      YYJSON_READ_NOFLAG);
  yyjson_doc *merge = parse(
      "{\"title\":\"Hello!\",\"phoneNumber\":\"+01-234-567-8910\","
      "\"author\":{\"familyName\":null},\"tags\":[\"example\"]}",
      YYJSON_READ_NOFLAG);
  yyjson_doc *patch = parse(
      "[{\"op\":\"add\",\"path\":\"/baz\",\"value\":\"qux\"},"
      "{\"op\":\"remove\",\"path\":\"/tags/1\"},"
      "{\"op\":\"replace\",\"path\":\"/author/givenName\",\"value\":\"Jane\"},"
      "{\"op\":\"test\",\"path\":\"/baz\",\"value\":\"qux\"}]",
      YYJSON_READ_NOFLAG);
  yyjson_doc *failing = parse(
      "[{\"op\":\"add\",\"path\":\"/x\",\"value\":1},"
      "{\"op\":\"test\",\"path\":\"/title\",\"value\":\"Hello!\"}]",
      YYJSON_READ_NOFLAG);
  yyjson_mut_doc *mdoc = yyjson_mut_doc_new(NULL);
  yyjson_patch_err err;
  yyjson_mut_val *result;

  result = yyjson_merge_patch(mdoc, yyjson_doc_get_root(target),
                              yyjson_doc_get_root(merge));
  CHECK(equals_json(mdoc, result,
                    "{\"title\":\"Hello!\",\"author\":{\"givenName\":\"John\"},"
                    "\"tags\":[\"example\"],"
                    "\"content\":\"This will be unchanged\","
                    "\"phoneNumber\":\"+01-234-567-8910\"}"));

  /* JSON Patch (RFC 6902): every operation applies in order, and a
     failing "test" rejects the whole patch, naming the 0-based index of
     the failing operation. */
  memset(&err, 0, sizeof(err));
  result = yyjson_patch(mdoc, yyjson_doc_get_root(target),
                        yyjson_doc_get_root(patch), &err);
  CHECK(equals_json(mdoc, result,
                    "{\"title\":\"Goodbye!\",\"author\":{\"givenName\":\"Jane\","
                    "\"familyName\":\"Doe\"},\"tags\":[\"example\"],"
                    "\"content\":\"This will be unchanged\",\"baz\":\"qux\"}"));
  memset(&err, 0, sizeof(err));
  CHECK(yyjson_patch(mdoc, yyjson_doc_get_root(target),
                     yyjson_doc_get_root(failing), &err) == NULL);
  CHECK(err.code == YYJSON_PATCH_ERROR_EQUAL && err.idx == 1);

  yyjson_mut_doc_free(mdoc);
  yyjson_doc_free(failing);
  yyjson_doc_free(patch);
  yyjson_doc_free(merge);
  yyjson_doc_free(target);
}

static void check_incremental(void) {
  char buf[sizeof(kDoc)];
  size_t len = sizeof(kDoc) - 1;
  size_t avail;
  int calls = 0;
  yyjson_incr_state *state;
  yyjson_doc *doc = NULL;
  yyjson_doc *whole = parse(kDoc, YYJSON_READ_NOFLAG);
  yyjson_read_err err;

  memcpy(buf, kDoc, sizeof(kDoc));
  state = yyjson_incr_new(buf, len, YYJSON_READ_NOFLAG, NULL);
  CHECK(state != NULL);
  /* Hand the reader 16 more bytes at a time until it has a document. */
  for (avail = 16; state != NULL; avail += 16) {
    if (avail > len) {
      avail = len;
    }
    ++calls;
    doc = yyjson_incr_read(state, avail, &err);
    if (doc != NULL || err.code != YYJSON_READ_ERROR_MORE || avail == len) {
      break;
    }
  }
  CHECK(doc != NULL && calls > 1);
  CHECK(yyjson_equals(yyjson_doc_get_root(doc), yyjson_doc_get_root(whole)));
  yyjson_incr_free(state);
  yyjson_doc_free(doc);
  yyjson_doc_free(whole);
}

static void check_pool(void) {
  /* A fixed buffer as the allocator: 4096 bytes hold kDoc's document;
     64 bytes cannot even hold the reader's copy of its text. */
  static char big[4096];
  static char tiny[64];
  yyjson_alc alc;
  yyjson_read_err err;
  yyjson_doc *doc;

  CHECK(yyjson_alc_pool_init(&alc, big, sizeof(big)));
  doc = yyjson_read_opts((char *)kDoc, strlen(kDoc), 0, &alc, &err);
  CHECK(doc != NULL);
  yyjson_doc_free(doc);
  CHECK(yyjson_alc_pool_init(&alc, tiny, sizeof(tiny)));
  memset(&err, 0, sizeof(err));
  CHECK(yyjson_read_opts((char *)kDoc, strlen(kDoc), 0, &alc, &err) == NULL);
  CHECK(err.code == YYJSON_READ_ERROR_MEMORY_ALLOCATION);
}

int main(void) {
  check_read_write();
  check_numbers();
  check_errors();
  check_non_standard();
  check_mutable();
  check_patches();
  check_incremental();
  check_pool();
  if (failures != 0) {
    return 1;
  }
  printf("yyjson %s: C API OK\n", YYJSON_VERSION_STRING);
  return 0;
}
