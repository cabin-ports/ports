#include <GKlib.h>

#include <cstdio>
#include <cstring>

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__,  \
                   #cond);                                                   \
      return 1;                                                              \
    }                                                                        \
  } while (0)

int main() {
  int a[5] = {5, 1, 4, 2, 3};
  CHECK(gk_isum(5, a, 1) == 15);
  CHECK(gk_imax(5, a, 1) == 5 && gk_iargmax(5, a, 1) == 0);
  gk_isorti(5, a);
  for (int i = 0; i < 5; i++)
    CHECK(a[i] == i + 1);

  gk_ikv_t kv[3] = {{3, 30}, {1, 10}, {2, 20}};
  gk_ikvsorti(3, kv);
  for (int i = 0; i < 3; i++)
    CHECK(kv[i].key == i + 1 && kv[i].val == 10 * (i + 1));

  char str[] = "hello world world";
  char pattern[] = "w(or)";
  char replacement[] = "W$1";
  char options[] = "g";
  char *s = nullptr;
  CHECK(gk_strstr_replace(str, pattern, replacement, options, &s) == 3);
  CHECK(std::strcmp(s, "hello World World") == 0);
  gk_free(reinterpret_cast<void **>(&s), LTERM);
  CHECK(s == nullptr);

  // gk_sigcatch() reads gk_externs.h's thread-local jump buffers here.
  char message[] = "expected error from the consumer test";
  volatile int caught = 0;
  CHECK(gk_sigtrap() == 1);
  switch (gk_sigcatch()) {
  case 0:
    gk_errexit(SIGERR, message);
    break;
  case SIGERR:
    caught = 1;
    break;
  default:
    break;
  }
  CHECK(gk_siguntrap() == 1);
  CHECK(caught == 1);
  return 0;
}
