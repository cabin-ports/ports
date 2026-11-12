// Checks utf8_range's C API on well-formed and malformed UTF-8, including
// buffers long enough to take the SSE4.1 or NEON path where it is built.
#include <stdio.h>
#include <string.h>

#include <utf8_range.h>

static int failures;

static void check(const char* what, const char* data, size_t len,
                  bool valid, size_t prefix) {
  bool got_valid = utf8_range_IsValid(data, len);
  size_t got_prefix = utf8_range_ValidPrefix(data, len);
  if (got_valid != valid || got_prefix != prefix) {
    fprintf(stderr, "%s: IsValid %d (want %d), ValidPrefix %zu (want %zu)\n",
            what, got_valid, valid, got_prefix, prefix);
    ++failures;
  }
}

#define CHECK(lit, valid, prefix) \
  check(#lit, lit, sizeof(lit) - 1, valid, prefix)

int main(void) {
  CHECK("", true, 0);
  CHECK("plain ASCII", true, 11);
  CHECK("a\0b", true, 3);
  CHECK("\xC3\xA9", true, 2);              // U+00E9
  CHECK("\xE0\xA0\x80", true, 3);          // U+0800, the lowest 3-byte form
  CHECK("\xED\x9F\xBF", true, 3);          // U+D7FF, just below surrogates
  CHECK("\xF0\x9F\x98\x80", true, 4);      // U+1F600
  CHECK("\xF4\x8F\xBF\xBF", true, 4);      // U+10FFFF
  CHECK("\xC0\x80", false, 0);             // overlong NUL
  CHECK("\xE0\x9F\xBF", false, 0);         // overlong U+07FF
  CHECK("\xED\xA0\x80", false, 0);         // surrogate U+D800
  CHECK("\xF4\x90\x80\x80", false, 0);     // above U+10FFFF
  CHECK("\x80", false, 0);                 // stray continuation byte
  CHECK("\xFF", false, 0);
  CHECK("ab\xE2\x82\xAC\xE2\x82", false, 5);  // truncated after a euro sign

  // 3 ASCII bytes, then 20 euro signs: long enough for the SIMD loop.
  char buf[3 + 20 * 3];
  memcpy(buf, "abc", 3);
  for (int i = 0; i < 20; ++i) memcpy(buf + 3 + i * 3, "\xE2\x82\xAC", 3);
  check("20 euro signs", buf, sizeof buf, true, sizeof buf);
  // Break the 13th euro sign's second byte: the valid prefix ends where it
  // starts.
  buf[3 + 12 * 3 + 1] = 'x';
  check("broken 13th euro sign", buf, sizeof buf, false, 3 + 12 * 3);

  // The lead bytes the SIMD paths special-case (E0, ED, F4), far enough in
  // for the SIMD loop: valid, then with one sequence at a time made invalid.
  static const char group[] = "\xE0\xA0\x80" "\xED\x9F\xBF" "\xF4\x8F\xBF\xBF";
  char edge[3 + 5 * 10];
  memcpy(edge, "abc", 3);
  for (int i = 0; i < 5; ++i) memcpy(edge + 3 + i * 10, group, 10);
  check("E0, ED and F4 sequences", edge, sizeof edge, true, sizeof edge);
  edge[17] = '\xA0';  // ED A0 BF at 16: a surrogate
  check("surrogate among them", edge, sizeof edge, false, 16);
  edge[17] = '\x9F';
  edge[24] = '\x9F';  // E0 9F 80 at 23: overlong
  check("overlong among them", edge, sizeof edge, false, 23);
  edge[24] = '\xA0';
  edge[30] = '\x90';  // F4 90 BF BF at 29: above U+10FFFF
  check("above U+10FFFF among them", edge, sizeof edge, false, 29);

  if (failures != 0) return 1;
  puts("utf8_range C API OK");
  return 0;
}
