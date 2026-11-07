/* utf8proc's C API from C99: decoding and encoding, character
   properties, case mapping, normalization, case folding and grapheme
   boundaries, against values from the Unicode 18 tables. */
#include <stdio.h>
#include <string.h>

#include <utf8proc.h>

static int failures = 0;

#define CHECK(cond)                                                    \
  do {                                                                 \
    if (!(cond)) {                                                     \
      fprintf(stderr, "main.c:%d: check failed: %s\n", __LINE__, #cond); \
      ++failures;                                                      \
    }                                                                  \
  } while (0)

static const utf8proc_uint8_t *u8(const char *s) {
  return (const utf8proc_uint8_t *)s;
}

/* Whether a NUL-terminated result equals `expected`, freeing it. */
static int take_equals(utf8proc_uint8_t *got, const char *expected) {
  int ok = got != NULL && strcmp((const char *)got, expected) == 0;
  utf8proc_free(got);
  return ok;
}

/* Whether utf8proc_map(in, options) produces exactly `expected`. */
static int map_equals(const char *in, unsigned int options,
                      const char *expected) {
  utf8proc_uint8_t *out = NULL;
  utf8proc_ssize_t n =
      utf8proc_map(u8(in), 0, &out, options | UTF8PROC_NULLTERM);
  int ok = n >= 0 && (size_t)n == strlen(expected) &&
           memcmp(out, expected, (size_t)n) == 0;
  utf8proc_free(out);
  return ok;
}

int main(void) {
  utf8proc_int32_t cp = 0;
  utf8proc_int32_t state = 0;
  utf8proc_uint8_t buf[4];
  utf8proc_uint8_t *out = NULL;

  CHECK(strcmp(utf8proc_version(), "2.12.0") == 0);
  CHECK(strcmp(utf8proc_unicode_version(), "18.0.0") == 0);

  /* Decoding, validation and encoding. */
  CHECK(utf8proc_iterate(u8("\xE2\x82\xAC"), -1, &cp) == 3 && cp == 0x20AC);
  CHECK(utf8proc_iterate(u8("\xC0\x80"), -1, &cp) ==
        UTF8PROC_ERROR_INVALIDUTF8);
  CHECK(utf8proc_iterate(u8("\xED\xA0\x80"), -1, &cp) ==
        UTF8PROC_ERROR_INVALIDUTF8);
  CHECK(utf8proc_codepoint_valid(0x10FFFF) && !utf8proc_codepoint_valid(0xD800));
  CHECK(utf8proc_encode_char(0x1F600, buf) == 4 &&
        memcmp(buf, "\xF0\x9F\x98\x80", 4) == 0);
  CHECK(utf8proc_utf8class[0x41] == 1 && utf8proc_utf8class[0xF0] == 4 &&
        utf8proc_utf8class[0x80] == 0);

  /* Properties. */
  CHECK(utf8proc_category('A') == UTF8PROC_CATEGORY_LU);
  CHECK(strcmp(utf8proc_category_string(0x0661), "Nd") == 0);
  /* U+20C1 SAUDI RIYAL SIGN, new in Unicode 17. */
  CHECK(utf8proc_category(0x20C1) == UTF8PROC_CATEGORY_SC);
  CHECK(utf8proc_charwidth('A') == 1 && utf8proc_charwidth(0x4E00) == 2 &&
        utf8proc_charwidth(0x0301) == 0);

  /* Simple case mappings. */
  CHECK(utf8proc_toupper(0x00E9) == 0x00C9);
  CHECK(utf8proc_tolower(0x0391) == 0x03B1);
  CHECK(utf8proc_totitle(0x01C6) == 0x01C5);
  CHECK(utf8proc_isupper(0x00C9) && utf8proc_islower(0x00E9));

  /* Normalization forms. */
  CHECK(take_equals(utf8proc_NFD(u8("\xC3\xA9")), "e\xCC\x81"));
  CHECK(take_equals(utf8proc_NFC(u8("e\xCC\x81")), "\xC3\xA9"));
  CHECK(take_equals(utf8proc_NFKC(u8("\xEF\xAC\x81")), "fi"));
  /* U+212B ANGSTROM SIGN -> U+00C5 -> U+00E5. */
  CHECK(take_equals(utf8proc_NFKC_Casefold(u8("\xE2\x84\xAB")), "\xC3\xA5"));

  /* utf8proc_map: full case folding, mark stripping, and grapheme
     markers combined with composition (fixed in 2.12.0). */
  CHECK(map_equals("Stra\xC3\x9F" "e",
                   UTF8PROC_CASEFOLD | UTF8PROC_COMPOSE | UTF8PROC_STABLE,
                   "strasse"));
  CHECK(map_equals("Cr\xC3\xA8me", UTF8PROC_COMPOSE | UTF8PROC_STRIPMARK,
                   "Creme"));
  CHECK(map_equals("e\xCC\x81x", UTF8PROC_CHARBOUND | UTF8PROC_COMPOSE,
                   "\xFF\xC3\xA9\xFFx"));
  CHECK(utf8proc_map(u8("a\xFF"), 2, &out, UTF8PROC_COMPOSE) ==
        UTF8PROC_ERROR_INVALIDUTF8);

  /* Extended grapheme clusters: a regional-indicator pair, then a
     second pair (U+1F1EF U+1F1F5 U+1F1FA U+1F1F8). */
  CHECK(!utf8proc_grapheme_break_stateful(0x1F1EF, 0x1F1F5, &state));
  CHECK(utf8proc_grapheme_break_stateful(0x1F1F5, 0x1F1FA, &state));
  CHECK(!utf8proc_grapheme_break_stateful(0x1F1FA, 0x1F1F8, &state));
  CHECK(!utf8proc_grapheme_break('e', 0x0301) && utf8proc_grapheme_break('a', 'b'));

  if (failures != 0) {
    fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  return 0;
}
