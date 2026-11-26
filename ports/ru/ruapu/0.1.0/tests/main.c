#include <stdio.h>

#define RUAPU_IMPLEMENTATION
#include "ruapu.h"

#define CHECK(cond)                                         \
  do {                                                      \
    if (!(cond)) {                                          \
      fprintf(stderr, "%s:%d: failed: %s\n", __FILE__, __LINE__, #cond); \
      return 1;                                             \
    }                                                       \
  } while (0)

int main(void) {
  /* Nothing is detected until ruapu_init() runs. */
#if defined(__x86_64__) || defined(_M_X64)
  CHECK(ruapu_supports("sse2") == 0);
#elif defined(__aarch64__) || defined(_M_ARM64)
  CHECK(ruapu_supports("neon") == 0);
#endif

  ruapu_init();

  /* The x86-64 and AArch64 ABIs guarantee these baselines. */
#if defined(__x86_64__) || defined(_M_X64)
  CHECK(ruapu_supports("sse") == 1);
  CHECK(ruapu_supports("sse2") == 1);
#elif defined(__aarch64__) || defined(_M_ARM64)
  CHECK(ruapu_supports("neon") == 1);
#endif

  /* Names match exactly, not by prefix or case. */
  CHECK(ruapu_supports("definitely-not-an-isa") == 0);
  CHECK(ruapu_supports("") == 0);
#if defined(__x86_64__) || defined(_M_X64)
  CHECK(ruapu_supports("sse4") == 0);
  CHECK(ruapu_supports("SSE2") == 0);
#elif defined(__aarch64__) || defined(_M_ARM64)
  CHECK(ruapu_supports("ne") == 0);
  CHECK(ruapu_supports("NEON") == 0);
#endif
  return 0;
}
