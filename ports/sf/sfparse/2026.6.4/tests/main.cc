/* The C++ consumer: parses RFC 9651 structured fields (items, a dictionary and
 * a list) and checks types, exact values and the error returns. */
#include <sfparse.h>

#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(cond)                                                     \
  do {                                                                  \
    if (!(cond)) {                                                      \
      fprintf(stderr, "sfparse C++ test failed: line %d: %s\n", __LINE__, \
              #cond);                                                   \
      ++failures;                                                       \
    }                                                                   \
  } while (0)

static void init(sfparse_parser *p, const char *s) {
  sfparse_parser_init(p, reinterpret_cast<const uint8_t *>(s), strlen(s));
}

static int vec_is(const sfparse_vec *v, const char *s) {
  return v->len == strlen(s) && (v->len == 0 || memcmp(v->base, s, v->len) == 0);
}

static int bad_item(const char *s) {
  sfparse_parser p;
  sfparse_value v;
  init(&p, s);
  return sfparse_parser_item(&p, &v) == SFPARSE_ERR_PARSE;
}

static void items(void) {
  sfparse_parser p;
  sfparse_value v;
  sfparse_vec out;
  uint8_t buf[32];

  init(&p, "-42");
  CHECK(sfparse_parser_item(&p, &v) == 0);
  CHECK(v.type == SFPARSE_TYPE_INTEGER && v.integer == -42);
  CHECK(sfparse_parser_item(&p, &v) == SFPARSE_ERR_EOF);

  init(&p, "999999999999999");
  CHECK(sfparse_parser_item(&p, &v) == 0 && v.integer == 999999999999999);

  init(&p, "-1.25");
  CHECK(sfparse_parser_item(&p, &v) == 0);
  CHECK(v.type == SFPARSE_TYPE_DECIMAL && v.decimal.numer == -125 &&
        v.decimal.denom == 100);

  init(&p, "\"a\\\"b\"");
  CHECK(sfparse_parser_item(&p, &v) == 0);
  CHECK(v.type == SFPARSE_TYPE_STRING &&
        (v.flags & SFPARSE_VALUE_FLAG_ESCAPED_STRING) != 0);
  out.base = buf;
  out.len = 0;
  sfparse_unescape(&out, &v.vec);
  CHECK(vec_is(&out, "a\"b"));

  init(&p, "foo/bar:1");
  CHECK(sfparse_parser_item(&p, &v) == 0);
  CHECK(v.type == SFPARSE_TYPE_TOKEN && vec_is(&v.vec, "foo/bar:1"));

  init(&p, ":aGVsbG8=:");
  CHECK(sfparse_parser_item(&p, &v) == 0);
  CHECK(v.type == SFPARSE_TYPE_BYTESEQ && vec_is(&v.vec, "aGVsbG8="));
  out.base = buf;
  out.len = 0;
  sfparse_base64decode(&out, &v.vec);
  CHECK(vec_is(&out, "hello"));

  init(&p, "?1");
  CHECK(sfparse_parser_item(&p, &v) == 0);
  CHECK(v.type == SFPARSE_TYPE_BOOLEAN && v.boolean == 1);

  init(&p, "@1659578233");
  CHECK(sfparse_parser_item(&p, &v) == 0);
  CHECK(v.type == SFPARSE_TYPE_DATE && v.integer == 1659578233);

  init(&p, "%\"f%c3%bcr\"");
  CHECK(sfparse_parser_item(&p, &v) == 0);
  CHECK(v.type == SFPARSE_TYPE_DISPSTRING);
  out.base = buf;
  out.len = 0;
  sfparse_pctdecode(&out, &v.vec);
  CHECK(vec_is(&out, "f\xc3\xbcr"));

  init(&p, "5;a=1;b");
  CHECK(sfparse_parser_item(&p, &v) == 0 && v.integer == 5);
  CHECK(sfparse_parser_param(&p, &out, &v) == 0);
  CHECK(vec_is(&out, "a") && v.type == SFPARSE_TYPE_INTEGER && v.integer == 1);
  CHECK(sfparse_parser_param(&p, &out, &v) == 0);
  CHECK(vec_is(&out, "b") && v.type == SFPARSE_TYPE_BOOLEAN && v.boolean == 1);
  CHECK(sfparse_parser_param(&p, &out, &v) == SFPARSE_ERR_EOF);
}

static void errors(void) {
  sfparse_parser p;
  sfparse_value v;

  CHECK(bad_item("1.2345"));             /* more than three fraction digits */
  CHECK(bad_item("1234567890123456"));   /* more than 15 integer digits */
  CHECK(bad_item("?2"));
  CHECK(bad_item("\"unterminated"));
  CHECK(bad_item("\"bad\\n\""));         /* only \" and \\ may be escaped */
  CHECK(bad_item(":aGVsbG8="));
  CHECK(bad_item("@"));
  CHECK(bad_item("%\"f%C3%BC\""));       /* percent escapes are lowercase */

  init(&p, "1 2");  /* trailing data is reported by the second call */
  CHECK(sfparse_parser_item(&p, &v) == 0 && v.integer == 1);
  CHECK(sfparse_parser_item(&p, &v) == SFPARSE_ERR_PARSE);
}

static void dictionary(void) {
  sfparse_parser p;
  sfparse_value v;
  sfparse_vec k;

  init(&p, "a=1, b=?0;x=\"y\", c=2.5, d=:aGVsbG8=:, e");

  CHECK(sfparse_parser_dict(&p, &k, &v) == 0);
  CHECK(vec_is(&k, "a") && v.type == SFPARSE_TYPE_INTEGER && v.integer == 1);

  CHECK(sfparse_parser_dict(&p, &k, &v) == 0);
  CHECK(vec_is(&k, "b") && v.type == SFPARSE_TYPE_BOOLEAN && v.boolean == 0);
  CHECK(sfparse_parser_param(&p, &k, &v) == 0);
  CHECK(vec_is(&k, "x") && v.type == SFPARSE_TYPE_STRING && vec_is(&v.vec, "y"));
  CHECK(sfparse_parser_param(&p, &k, &v) == SFPARSE_ERR_EOF);

  CHECK(sfparse_parser_dict(&p, &k, &v) == 0);
  CHECK(vec_is(&k, "c") && v.type == SFPARSE_TYPE_DECIMAL &&
        v.decimal.numer == 25 && v.decimal.denom == 10);

  CHECK(sfparse_parser_dict(&p, &k, &v) == 0);
  CHECK(vec_is(&k, "d") && v.type == SFPARSE_TYPE_BYTESEQ);

  CHECK(sfparse_parser_dict(&p, &k, &v) == 0);
  CHECK(vec_is(&k, "e") && v.type == SFPARSE_TYPE_BOOLEAN && v.boolean == 1);

  CHECK(sfparse_parser_dict(&p, &k, &v) == SFPARSE_ERR_EOF);

  init(&p, "a=1,");  /* a trailing comma is an error */
  CHECK(sfparse_parser_dict(&p, &k, &v) == 0);
  CHECK(sfparse_parser_dict(&p, &k, &v) == SFPARSE_ERR_PARSE);
}

static void list(void) {
  sfparse_parser p;
  sfparse_value v;
  sfparse_vec k;

  init(&p, "(1 2);p=3, \"x\"");

  CHECK(sfparse_parser_list(&p, &v) == 0 && v.type == SFPARSE_TYPE_INNER_LIST);
  CHECK(sfparse_parser_inner_list(&p, &v) == 0 && v.integer == 1);
  CHECK(sfparse_parser_inner_list(&p, &v) == 0 && v.integer == 2);
  CHECK(sfparse_parser_inner_list(&p, &v) == SFPARSE_ERR_EOF);
  CHECK(sfparse_parser_param(&p, &k, &v) == 0);
  CHECK(vec_is(&k, "p") && v.type == SFPARSE_TYPE_INTEGER && v.integer == 3);
  CHECK(sfparse_parser_param(&p, &k, &v) == SFPARSE_ERR_EOF);

  CHECK(sfparse_parser_list(&p, &v) == 0);
  CHECK(v.type == SFPARSE_TYPE_STRING && vec_is(&v.vec, "x"));
  CHECK(sfparse_parser_list(&p, &v) == SFPARSE_ERR_EOF);
}

int main(void) {
  items();
  errors();
  dictionary();
  list();
  if (failures != 0) {
    return 1;
  }
  puts("sfparse C++ test passed");
  return 0;
}
