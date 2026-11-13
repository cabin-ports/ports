// C++98.  The other documented arrangement: JSMN_STATIC makes this
// translation unit's copy of the API static (README.md:95-98), and
// JSMN_STRICT and JSMN_PARENT_LINKS select the strict parser and the
// jsmntok_t::parent field (jsmn.h:74-76).
#define JSMN_STATIC
#define JSMN_STRICT
#define JSMN_PARENT_LINKS
#include <jsmn.h>

#include <cstdio>
#include <cstring>

namespace {

int failures = 0;

#define CHECK(cond)                                                        \
  do {                                                                     \
    if (!(cond)) {                                                         \
      ++failures;                                                          \
      std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__,         \
                   __LINE__, #cond);                                       \
    }                                                                      \
  } while (0)

struct expect {
  jsmntype_t type;
  int start;
  int end;
  int size;
  int parent;
};

int parse(const char *js, std::size_t len, jsmntok_t *tok,
          unsigned int num_tokens) {
  jsmn_parser p;
  jsmn_init(&p);
  return jsmn_parse(&p, js, len, tok, num_tokens);
}

int parse(const char *js) {
  jsmntok_t tok[32];
  return parse(js, std::strlen(js), tok, 32);
}

bool same(const jsmntok_t *tok, const expect *want, int n) {
  for (int i = 0; i < n; ++i) {
    if (tok[i].type != want[i].type || tok[i].start != want[i].start ||
        tok[i].end != want[i].end || tok[i].size != want[i].size ||
        tok[i].parent != want[i].parent) {
      std::fprintf(stderr, "token %d: type %d [%d, %d) size %d parent %d\n",
                   i, static_cast<int>(tok[i].type), tok[i].start,
                   tok[i].end, tok[i].size, tok[i].parent);
      return false;
    }
  }
  return true;
}

// parent is the index of the enclosing container, or of the key whose value
// the token is; -1 at the top level.
const char doc[] = "{\"name\":\"jsmn\",\"tags\":[1,true,null],"
                   "\"n\":{\"x\":-2.5}}";
const expect doc_tokens[] = {
    {JSMN_OBJECT, 0, 51, 3, -1},     {JSMN_STRING, 2, 6, 1, 0},
    {JSMN_STRING, 9, 13, 0, 1},      {JSMN_STRING, 16, 20, 1, 0},
    {JSMN_ARRAY, 22, 35, 3, 3},      {JSMN_PRIMITIVE, 23, 24, 0, 4},
    {JSMN_PRIMITIVE, 25, 29, 0, 4},  {JSMN_PRIMITIVE, 30, 34, 0, 4},
    {JSMN_STRING, 37, 38, 1, 0},     {JSMN_OBJECT, 40, 50, 1, 8},
    {JSMN_STRING, 42, 43, 1, 9},     {JSMN_PRIMITIVE, 45, 49, 0, 10}};

// The comma inside the inner object must not end it.
const char nested[] = "{\"o\":{\"a\":1,\"b\":2},\"c\":3}";
const expect nested_tokens[] = {
    {JSMN_OBJECT, 0, 25, 2, -1},    {JSMN_STRING, 2, 3, 1, 0},
    {JSMN_OBJECT, 5, 18, 2, 1},     {JSMN_STRING, 7, 8, 1, 2},
    {JSMN_PRIMITIVE, 10, 11, 0, 3}, {JSMN_STRING, 13, 14, 1, 2},
    {JSMN_PRIMITIVE, 16, 17, 0, 5}, {JSMN_STRING, 20, 21, 1, 0},
    {JSMN_PRIMITIVE, 23, 24, 0, 7}};

const char stream[] = "{\"n\":123}";
const expect stream_tokens[] = {{JSMN_OBJECT, 0, 9, 1, -1},
                                {JSMN_STRING, 2, 3, 1, 0},
                                {JSMN_PRIMITIVE, 5, 8, 0, 1}};

} // namespace

int main() {
  jsmn_parser p;
  jsmntok_t tok[32];

  CHECK(parse(doc, std::strlen(doc), tok, 32) == 12);
  CHECK(same(tok, doc_tokens, 12));
  CHECK(parse(doc, std::strlen(doc), NULL, 0) == 12);
  CHECK(parse(doc, std::strlen(doc), tok, 11) == JSMN_ERROR_NOMEM);
  CHECK(parse(nested, std::strlen(nested), tok, 32) == 9);
  CHECK(same(tok, nested_tokens, 9));

  // Strict mode refuses input the default parser accepts:
  // unquoted keys, primitive and container keys, a second value for a key,
  // and other bare characters (jsmn.h:290-295, 410-417, 432-436).
  CHECK(parse("{a:1}") == JSMN_ERROR_INVAL);
  CHECK(parse("{1:2}") == JSMN_ERROR_INVAL);
  CHECK(parse("{[]:1}") == JSMN_ERROR_INVAL);
  CHECK(parse("{\"a\":1 2}") == JSMN_ERROR_INVAL);
  CHECK(parse("[x]") == JSMN_ERROR_INVAL);

  // With parent links, closing brackets walk the parents.
  CHECK(parse("{\"a\":[1}") == JSMN_ERROR_INVAL);
  CHECK(parse("]") == JSMN_ERROR_INVAL);
  CHECK(parse("[1]]") == JSMN_ERROR_INVAL);

  // A strict primitive needs its delimiter, so input that ends inside one is
  // JSMN_ERROR_PART (jsmn.h:163-167), and the same parser resumes once more
  // data arrives (README.md:171-173).
  jsmn_init(&p);
  CHECK(jsmn_parse(&p, stream, 7, tok, 32) == JSMN_ERROR_PART);
  CHECK(jsmn_parse(&p, stream, std::strlen(stream), tok, 32) == 3);
  CHECK(same(tok, stream_tokens, 3));
  CHECK(parse("{\"a\":[1,2]") == JSMN_ERROR_PART);

  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("jsmn: ok");
  return 0;
}
