/* C89, upstream's floor (README.md:35).  This translation unit holds the
 * implementation: jsmn.h with none of its macros, so non-strict parsing
 * without parent links.  tests/main.cc covers JSMN_STATIC, JSMN_STRICT and
 * JSMN_PARENT_LINKS. */
#include <jsmn.h>

#include <stdio.h>
#include <string.h>

static int failures = 0;

/* Unaligned continuations keep the logical line within C89's 509
 * characters. */
#define CHECK(cond) \
  do { \
    if (!(cond)) { \
      ++failures; \
      fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, \
              #cond); \
    } \
  } while (0)

struct expect {
  jsmntype_t type;
  int start;
  int end;
  int size;
};

static int parse(const char *js, size_t len, jsmntok_t *tok,
                 unsigned int num_tokens) {
  jsmn_parser p;
  jsmn_init(&p);
  return jsmn_parse(&p, js, len, tok, num_tokens);
}

static int same(const jsmntok_t *tok, const struct expect *want, int n) {
  int i;
  for (i = 0; i < n; ++i) {
    if (tok[i].type != want[i].type || tok[i].start != want[i].start ||
        tok[i].end != want[i].end || tok[i].size != want[i].size) {
      fprintf(stderr, "token %d: type %d [%d, %d) size %d\n", i,
              (int)tok[i].type, tok[i].start, tok[i].end, tok[i].size);
      return 0;
    }
  }
  return 1;
}

/* The expected tokens of js, in document order; also checks the count
 * a NULL token array reports and that n - 1 tokens are too few. */
static void check_tokens(const char *js, const struct expect *want, int n) {
  jsmntok_t tok[32];
  CHECK(parse(js, strlen(js), tok, 32) == n);
  CHECK(same(tok, want, n));
  CHECK(parse(js, strlen(js), NULL, 0) == n);
  CHECK(parse(js, strlen(js), tok, (unsigned int)n) == n);
  CHECK(parse(js, strlen(js), tok, (unsigned int)n - 1) == JSMN_ERROR_NOMEM);
}

/* Offsets are byte positions with the end one past the token
 * (README.md:59-60); strings exclude their quotes, and a key's size counts
 * its value (jsmn.h:373-375). */
static const char doc[] = "{\"name\":\"jsmn\",\"tags\":[1,true,null],"
                          "\"n\":{\"x\":-2.5}}";
static const struct expect doc_tokens[] = {
    {JSMN_OBJECT, 0, 51, 3},     {JSMN_STRING, 2, 6, 1},
    {JSMN_STRING, 9, 13, 0},     {JSMN_STRING, 16, 20, 1},
    {JSMN_ARRAY, 22, 35, 3},     {JSMN_PRIMITIVE, 23, 24, 0},
    {JSMN_PRIMITIVE, 25, 29, 0}, {JSMN_PRIMITIVE, 30, 34, 0},
    {JSMN_STRING, 37, 38, 1},    {JSMN_OBJECT, 40, 50, 1},
    {JSMN_STRING, 42, 43, 1},    {JSMN_PRIMITIVE, 45, 49, 0}};

/* README.md:50-60's example, with the offsets it gives. */
static const char readme[] = "{ \"name\" : \"Jack\", \"age\" : 27 }";
static const struct expect readme_tokens[] = {{JSMN_OBJECT, 0, 31, 2},
                                              {JSMN_STRING, 3, 7, 1},
                                              {JSMN_STRING, 12, 16, 0},
                                              {JSMN_STRING, 20, 23, 1},
                                              {JSMN_PRIMITIVE, 27, 29, 0}};

/* Escapes stay in the token's span, raw. */
static const char escapes[] = "[ \"a\\\"b\", \"\\u00e9\", -1e3 ]";
static const struct expect escapes_tokens[] = {{JSMN_ARRAY, 0, 26, 3},
                                               {JSMN_STRING, 3, 7, 0},
                                               {JSMN_STRING, 11, 17, 0},
                                               {JSMN_PRIMITIVE, 20, 24, 0}};

/* The comma inside the inner object must not end it. */
static const char nested[] = "{\"o\":{\"a\":1,\"b\":2},\"c\":3}";
static const struct expect nested_tokens[] = {
    {JSMN_OBJECT, 0, 25, 2},     {JSMN_STRING, 2, 3, 1},
    {JSMN_OBJECT, 5, 18, 2},     {JSMN_STRING, 7, 8, 1},
    {JSMN_PRIMITIVE, 10, 11, 0}, {JSMN_STRING, 13, 14, 1},
    {JSMN_PRIMITIVE, 16, 17, 0}, {JSMN_STRING, 20, 21, 1},
    {JSMN_PRIMITIVE, 23, 24, 0}};

/* Empty containers; the comma after 1 belongs to the outer object, not to
 * the closed array before it. */
static const char empty[] = "{\"a\":[],\"b\":1,\"c\":{}}";
static const struct expect empty_tokens[] = {
    {JSMN_OBJECT, 0, 21, 3},     {JSMN_STRING, 2, 3, 1},
    {JSMN_ARRAY, 5, 7, 0},       {JSMN_STRING, 9, 10, 1},
    {JSMN_PRIMITIVE, 12, 13, 0}, {JSMN_STRING, 15, 16, 1},
    {JSMN_OBJECT, 18, 20, 0}};

/* Without JSMN_STRICT every unquoted run is a primitive, keys included. */
static const char loose[] = "{a:1}";
static const struct expect loose_tokens[] = {{JSMN_OBJECT, 0, 5, 1},
                                             {JSMN_PRIMITIVE, 1, 2, 1},
                                             {JSMN_PRIMITIVE, 3, 4, 0}};

static const char stream[] = "{\"key\":\"value\"}";
static const struct expect stream_tokens[] = {{JSMN_OBJECT, 0, 15, 1},
                                              {JSMN_STRING, 2, 5, 1},
                                              {JSMN_STRING, 8, 13, 0}};

int main(void) {
  jsmn_parser p;
  jsmntok_t tok[32];
  static const char nul[] = "[1]\0[2]";
  static const char hex[] = "[\"\\uFfFf\"]";

  check_tokens(doc, doc_tokens, 12);
  check_tokens(readme, readme_tokens, 5);
  check_tokens(escapes, escapes_tokens, 4);
  check_tokens(nested, nested_tokens, 9);
  check_tokens(empty, empty_tokens, 7);
  check_tokens(loose, loose_tokens, 3);
  CHECK(strncmp(doc + doc_tokens[2].start, "jsmn", 4) == 0);
  CHECK(strncmp(doc + doc_tokens[11].start, "-2.5", 4) == 0);

  /* After JSMN_ERROR_NOMEM, the same parser resumes with more tokens
   * (README.md:170-171), and the count covers the whole input.  The parser
   * runs out at a string, a primitive and an object in turn. */
  jsmn_init(&p);
  CHECK(jsmn_parse(&p, doc, strlen(doc), tok, 2) == JSMN_ERROR_NOMEM);
  CHECK(jsmn_parse(&p, doc, strlen(doc), tok, 5) == JSMN_ERROR_NOMEM);
  CHECK(jsmn_parse(&p, doc, strlen(doc), tok, 9) == JSMN_ERROR_NOMEM);
  CHECK(jsmn_parse(&p, doc, strlen(doc), tok, 32) == 12);
  CHECK(same(tok, doc_tokens, 12));

  /* Input cut inside a string is JSMN_ERROR_PART; the same parser then
   * resumes on the longer input (README.md:171-173). */
  jsmn_init(&p);
  CHECK(jsmn_parse(&p, stream, 11, tok, 32) == JSMN_ERROR_PART);
  CHECK(jsmn_parse(&p, stream, strlen(stream), tok, 32) == 3);
  CHECK(same(tok, stream_tokens, 3));

  /* Unclosed containers and strings. */
  CHECK(parse("{\"a\":[1,2", 9, tok, 32) == JSMN_ERROR_PART);
  CHECK(parse("{\"a\":\"xy", 8, tok, 32) == JSMN_ERROR_PART);
  CHECK(parse("[", 1, tok, 32) == JSMN_ERROR_PART);

  /* Corrupt input. */
  CHECK(parse("{\"a\":\"\\q\"}", 10, tok, 32) == JSMN_ERROR_INVAL);
  CHECK(parse("{\"a\":\"\\u12G4\"}", 14, tok, 32) == JSMN_ERROR_INVAL);
  CHECK(parse("[1,2}", 5, tok, 32) == JSMN_ERROR_INVAL);
  CHECK(parse("]", 1, tok, 32) == JSMN_ERROR_INVAL);
  CHECK(parse("[1\001]", 4, tok, 32) == JSMN_ERROR_INVAL);
  CHECK(parse("[1\177]", 4, tok, 32) == JSMN_ERROR_INVAL);

  /* The hex digits of an escape may be either case. */
  CHECK(parse(hex, strlen(hex), tok, 32) == 2);
  CHECK(tok[1].type == JSMN_STRING && tok[1].start == 2 && tok[1].end == 8);

  /* A NUL ends the input before len does, also inside a primitive or a
   * string. */
  CHECK(parse(nul, sizeof nul - 1, tok, 32) == 2);
  CHECK(tok[0].type == JSMN_ARRAY && tok[0].end == 3 && tok[0].size == 1);
  CHECK(parse("[12\0]", 5, tok, 32) == JSMN_ERROR_PART);
  CHECK(parse("[\"a\0\"]", 6, tok, 32) == JSMN_ERROR_PART);

  if (failures != 0) {
    fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  puts("jsmn: ok");
  return 0;
}
