#include <utf8.h>

#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond)                                                            \
  do {                                                                         \
    if (!(cond)) {                                                             \
      fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond); \
      ++failures;                                                              \
    }                                                                          \
  } while (0)

int main(void) {
  /* "hello" with U+00E9 as its second code point, and its upper case. */
  const char s[] = "h\xc3\xa9llo";
  const char upper[] = "H\xc3\x89LLO";
  /* 'a', U+20AC and U+20BB7: complete 3- and 4-byte sequences. */
  const char wide[] = "a\xe2\x82\xac\xf0\xa0\xae\xb7";
  /* Each is invalid from its first byte: a UTF-16 surrogate (U+D800),
   * overlong 2-, 3- and 4-byte forms, code points above U+10FFFF, and bad
   * continuation bytes. */
  static const char *const invalid[] = {
      "\xed\xa0\x80",     "\xc0\xaf",         "\xe0\x80\xaf",
      "\xf0\x80\x80\xaf", "\xf4\x90\x80\x80", "\xf5\x80\x80\x80",
      "\xe2\x28\xa1",     "\xf0\x28\x8c\xbc"};
  const char truncated[] = "ab\xc3";
  const char stray[] = "a\x80";
  const char cut3[] = "a\xe2\x82";
  char buf[sizeof s];
  utf8_int32_t cp = 0;
  const char *next;
  size_t i;

  CHECK(utf8len(s) == 5);
  CHECK(utf8size(s) == 7);
  CHECK(utf8valid(s) == NULL);
  CHECK(utf8casecmp(upper, s) == 0);
  CHECK(utf8casecmp(s, "H\xc3\x89LLP") < 0);
  CHECK(utf8casecmp("\xc3\xa9", "\xc3\xa8") > 0);
  CHECK(utf8casecmp("ab", "abc") < 0);
  CHECK(utf8cmp(upper, "H\xc3\x89LLO") == 0);
  CHECK(utf8cmp("abc", "abd") < 0);
  CHECK(utf8cmp("abd", "abc") > 0);
  CHECK(utf8cmp("ab", "abc") < 0);
  /* Bytes compare unsigned, so a lead byte sorts after ASCII. */
  CHECK(utf8cmp("\xc3\xa9", "z") > 0);
  CHECK(utf8chr(s, 0xE9) == s + 1);
  CHECK(utf8str(s, "ll") == s + 3);
  CHECK(utf8str(s, "lo") == s + 4);
  CHECK(utf8str(s, "lx") == NULL);

  next = utf8codepoint(s + 1, &cp);
  CHECK(cp == 0xE9 && next == s + 3);

  CHECK(utf8len(wide) == 3);
  CHECK(utf8valid(wide) == NULL);
  next = utf8codepoint(wide + 1, &cp);
  CHECK(cp == 0x20AC && next == wide + 4);
  next = utf8codepoint(wide + 4, &cp);
  CHECK(cp == 0x20BB7 && next == wide + 8);
  CHECK(utf8chr(wide, 0x20AC) == wide + 1);
  CHECK(utf8chr(wide, 0x20BB7) == wide + 4);

  memcpy(buf, s, sizeof s);
  utf8upr(buf);
  CHECK(memcmp(buf, upper, sizeof upper) == 0);
  utf8lwr(buf);
  CHECK(memcmp(buf, s, sizeof s) == 0);

  for (i = 0; i < sizeof invalid / sizeof invalid[0]; ++i) {
    CHECK(utf8valid(invalid[i]) == invalid[i]);
  }
  CHECK(utf8valid(truncated) == truncated + 2);
  CHECK(utf8valid(stray) == stray + 1);
  /* Truncated sequences end at the NUL or the byte limit rather than reading
   * past it. */
  CHECK(utf8len(cut3) == 2);
  CHECK(utf8codepoint(cut3 + 1, &cp) == cut3 + 3);
  CHECK(utf8nlen(s, 2) == 1);
  CHECK(utf8nlen(s, 3) == 2);

  if (failures != 0) {
    fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  puts("utf8h ok (C)");
  return 0;
}
