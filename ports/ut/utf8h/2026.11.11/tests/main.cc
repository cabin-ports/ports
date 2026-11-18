#include <utf8.h>

#include <cstddef>
#include <cstdio>
#include <cstring>

namespace {

int failures = 0;

#define CHECK(cond)                                                            \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__,    \
                   #cond);                                                     \
      ++failures;                                                              \
    }                                                                          \
  } while (0)

} // namespace

// utf8_int8_t is char8_t under utf8.h:139's condition, and char8_t arrays take
// only u8 literals.
#if defined(utf8_cplusplus) && utf8_cplusplus >= 202002L &&                    \
    defined(__cpp_char8_t)
#define U8(s) u8##s
#else
#define U8(s) s
#endif

// The same checks as main.c.
int main() {
  const utf8_int8_t s[] = U8("h\xc3\xa9llo");
  const utf8_int8_t upper[] = U8("H\xc3\x89LLO");
  const utf8_int8_t wide[] = U8("a\xe2\x82\xac\xf0\xa0\xae\xb7");
  static const utf8_int8_t *const invalid[] = {
      U8("\xed\xa0\x80"),     U8("\xc0\xaf"),
      U8("\xe0\x80\xaf"),     U8("\xf0\x80\x80\xaf"),
      U8("\xf4\x90\x80\x80"), U8("\xf5\x80\x80\x80"),
      U8("\xe2\x28\xa1"),     U8("\xf0\x28\x8c\xbc")};
  const utf8_int8_t truncated[] = U8("ab\xc3");
  const utf8_int8_t stray[] = U8("a\x80");
  const utf8_int8_t cut3[] = U8("a\xe2\x82");
  utf8_int8_t buf[sizeof s];
  utf8_int32_t cp = 0;
  const utf8_int8_t *next;

  CHECK(utf8len(s) == 5);
  CHECK(utf8size(s) == 7);
  CHECK(utf8valid(s) == NULL);
  CHECK(utf8casecmp(upper, s) == 0);
  CHECK(utf8casecmp(s, U8("H\xc3\x89LLP")) < 0);
  CHECK(utf8casecmp(U8("\xc3\xa9"), U8("\xc3\xa8")) > 0);
  CHECK(utf8casecmp(U8("ab"), U8("abc")) < 0);
  CHECK(utf8cmp(upper, U8("H\xc3\x89LLO")) == 0);
  CHECK(utf8cmp(U8("abc"), U8("abd")) < 0);
  CHECK(utf8cmp(U8("abd"), U8("abc")) > 0);
  CHECK(utf8cmp(U8("ab"), U8("abc")) < 0);
  CHECK(utf8cmp(U8("\xc3\xa9"), U8("z")) > 0);
  CHECK(utf8chr(s, 0xE9) == s + 1);
  CHECK(utf8str(s, U8("ll")) == s + 3);
  CHECK(utf8str(s, U8("lo")) == s + 4);
  CHECK(utf8str(s, U8("lx")) == NULL);

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

  std::memcpy(buf, s, sizeof s);
  utf8upr(buf);
  CHECK(std::memcmp(buf, upper, sizeof upper) == 0);
  utf8lwr(buf);
  CHECK(std::memcmp(buf, s, sizeof s) == 0);

  for (std::size_t i = 0; i < sizeof invalid / sizeof invalid[0]; ++i) {
    CHECK(utf8valid(invalid[i]) == invalid[i]);
  }
  CHECK(utf8valid(truncated) == truncated + 2);
  CHECK(utf8valid(stray) == stray + 1);
  CHECK(utf8len(cut3) == 2);
  CHECK(utf8codepoint(cut3 + 1, &cp) == cut3 + 3);
  CHECK(utf8nlen(s, 2) == 1);
  CHECK(utf8nlen(s, 3) == 2);

  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("utf8h ok (C++)");
  return 0;
}
