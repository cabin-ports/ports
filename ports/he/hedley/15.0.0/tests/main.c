/* A C89 consumer of hedley: version and token macros, casts, branch
 * hints, function attributes, and the compiler-version detection. */
#include <hedley.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond)                                          \
  do {                                                       \
    if (!(cond)) {                                           \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      ++failures;                                            \
    }                                                        \
  } while (0)

HEDLEY_STATIC_ASSERT(HEDLEY_VERSION == 15, "hedley 15");

#define PREFIX hedley_
#define SUFFIX answer

static int hedley_answer(void) { return 42; }

HEDLEY_WARN_UNUSED_RESULT
HEDLEY_NON_NULL(1, 2)
HEDLEY_PRINTF_FORMAT(2, 3)
static int format_into(char* buf, const char* fmt, ...) {
  int written;
  va_list ap;
  va_start(ap, fmt);
  written = vsprintf(buf, fmt, ap);
  va_end(ap);
  return written;
}

static int sum(size_t n, const int values[HEDLEY_ARRAY_PARAM(n)]) {
  size_t i;
  int total = 0;
  for (i = 0; i < n; ++i) total += values[i];
  return total;
}

static void copy(char* HEDLEY_RESTRICT dst, const char* HEDLEY_RESTRICT src,
                 size_t n) {
  size_t i;
  for (i = 0; i < n; ++i) dst[i] = src[i];
}

static int classify(int n) {
  int score = 0;
  switch (n) {
    case 1:
      score += 10;
      HEDLEY_FALL_THROUGH;
    case 2:
      score += 1;
      break;
    default:
      score = -1;
      break;
  }
  return score;
}

HEDLEY_ALWAYS_INLINE
static int twice(int x) { return 2 * x; }

HEDLEY_NEVER_INLINE
static int thrice(int x) { return 3 * x; }

HEDLEY_CONST
static int square(int x) { return x * x; }

static int last_digit(int x) {
  HEDLEY_ASSUME(x >= 0);
  return x % 10;
}

HEDLEY_DEPRECATED(15)
static int old_api(void) { return 7; }

HEDLEY_DIAGNOSTIC_PUSH
HEDLEY_DIAGNOSTIC_DISABLE_DEPRECATED
static int call_old_api(void) { return old_api(); }
HEDLEY_DIAGNOSTIC_POP

static long gcc_version(void) {
#if defined(HEDLEY_GCC_VERSION)
  return HEDLEY_GCC_VERSION;
#else
  return -1;
#endif
}

int main(void) {
  static const int values[] = {1, 2, 3};
  const char* text = "hedley";
  char* mutable_text;
  char buf[32];

  CHECK(HEDLEY_VERSION == 15);
  CHECK(HEDLEY_VERSION_ENCODE(1, 2, 3) == 1002003);
  CHECK(HEDLEY_VERSION_DECODE_MAJOR(HEDLEY_VERSION_ENCODE(1, 2, 3)) == 1);
  CHECK(HEDLEY_VERSION_DECODE_MINOR(HEDLEY_VERSION_ENCODE(1, 2, 3)) == 2);
  CHECK(HEDLEY_VERSION_DECODE_REVISION(HEDLEY_VERSION_ENCODE(1, 2, 3)) == 3);
  CHECK(strcmp(HEDLEY_STRINGIFY(HEDLEY_VERSION), "15") == 0);
  CHECK(strcmp(HEDLEY_STRINGIFY(HEDLEY_CONCAT3(foo, _, bar)), "foo_bar") == 0);
  CHECK(HEDLEY_CONCAT(PREFIX, SUFFIX)() == 42);

  CHECK(HEDLEY_STATIC_CAST(int, 3.75) == 3);
  mutable_text = HEDLEY_CONST_CAST(char*, text);
  CHECK(mutable_text == text);
  CHECK(HEDLEY_REINTERPRET_CAST(const unsigned char*, text)[0] == 'h');
  CHECK(HEDLEY_NULL == NULL);

  CHECK(HEDLEY_LIKELY(7) == 1);
  CHECK(HEDLEY_UNLIKELY(0) == 0);
#if !defined(_MSC_VER) || defined(__clang__)
  /* See cabin.toml: these do not compile in C on MSVC. */
  CHECK(HEDLEY_REQUIRE_CONSTEXPR(6 * 7) == 42);
  CHECK(HEDLEY_IS_CONSTANT(6 * 7));
#endif

  CHECK(format_into(buf, "%s-%d", "v", HEDLEY_VERSION) == 4);
  CHECK(strcmp(buf, "v-15") == 0);
  CHECK(sum(3, values) == 6);
  copy(buf, "abc", 4);
  CHECK(strcmp(buf, "abc") == 0);
  CHECK(classify(1) == 11);
  CHECK(classify(2) == 1);
  CHECK(classify(3) == -1);
  CHECK(twice(thrice(square(2))) == 24);
  CHECK(last_digit(1234) == 4);
  CHECK(call_old_api() == 7);

#if defined(__GNUC__)
  CHECK(HEDLEY_GNUC_VERSION ==
        HEDLEY_VERSION_ENCODE(__GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__));
  CHECK(HEDLEY_GNUC_VERSION_CHECK(__GNUC__, __GNUC_MINOR__, 0));
  CHECK(!HEDLEY_GNUC_VERSION_CHECK(99, 0, 0));
#else
  CHECK(!HEDLEY_GNUC_VERSION_CHECK(1, 0, 0));
#endif
#if defined(__GNUC__) && !defined(__clang__)
  CHECK(gcc_version() == HEDLEY_GNUC_VERSION);
#else
  CHECK(gcc_version() == -1);
#endif
#if defined(_MSC_VER)
  CHECK(HEDLEY_VERSION_DECODE_MAJOR(HEDLEY_MSVC_VERSION) == _MSC_VER / 100);
  CHECK(HEDLEY_VERSION_DECODE_MINOR(HEDLEY_MSVC_VERSION) == _MSC_VER % 100);
  CHECK(HEDLEY_MSVC_VERSION_CHECK(19, 0, 0));
  CHECK(!HEDLEY_MSVC_VERSION_CHECK(99, 0, 0));
#else
  CHECK(!HEDLEY_MSVC_VERSION_CHECK(1, 0, 0));
#endif

  if (failures != 0) {
    printf("%d failures\n", failures);
    return 1;
  }
  printf("hedley (C): ok\n");
  return 0;
}
