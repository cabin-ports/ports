#ifndef _WIN32
#include <regex.h>
#endif
#include <GKlib.h>

#include <stdio.h>
#include <string.h>

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); \
      return 1;                                                              \
    }                                                                        \
  } while (0)

int main(void) {
  int a[5] = {5, 1, 4, 2, 3};
  gk_ikv_t kv[3] = {{3, 30}, {1, 10}, {2, 20}};
  char *s = NULL;
  volatile int caught = 0;
  gk_csr_t *mat;
  int i;

  CHECK(gk_isum(5, a, 1) == 15);
  CHECK(gk_imax(5, a, 1) == 5 && gk_iargmax(5, a, 1) == 0);
  CHECK(gk_imin(5, a, 1) == 1 && gk_iargmin(5, a, 1) == 1);
  gk_isorti(5, a);
  for (i = 0; i < 5; i++)
    CHECK(a[i] == i + 1);
  gk_isortd(5, a);
  for (i = 0; i < 5; i++)
    CHECK(a[i] == 5 - i);
  gk_ikvsorti(3, kv);
  for (i = 0; i < 3; i++)
    CHECK(kv[i].key == i + 1 && kv[i].val == 10 * (i + 1));

  /* The library's regex: libc's off Windows, gkregex.c on it. */
  CHECK(gk_strstr_replace("hello world world", "w(or)", "W$1", "g", &s) == 3);
  CHECK(strcmp(s, "hello World World") == 0);
  gk_free((void **)&s, LTERM);
  CHECK(gk_strstr_replace("Hello HELLO", "hello", "bye", "i", &s) == 2);
  CHECK(strcmp(s, "bye HELLO") == 0);
  gk_free((void **)&s, LTERM);

#ifndef _WIN32
  {
    regex_t re;
    regmatch_t m[2];
    unsigned int seed1 = 7, seed2 = 7;
    int r;
    CHECK(regcomp(&re, "w(or)ld", REG_EXTENDED) == 0);
    CHECK(regexec(&re, "hello world", 2, m, 0) == 0);
    CHECK(m[1].rm_so == 7 && m[1].rm_eo == 9);
    regfree(&re);

    /* RandomInRange_r expands to POSIX rand_r; taking its address fails to
       compile where it is undeclared. */
    (void)&rand_r;
    r = RandomInRange_r(&seed1, 10);
    CHECK(r >= 0 && r < 10);
    CHECK(RandomInRange_r(&seed2, 10) == r && seed2 == seed1);
  }
#endif

  /* gk_sigcatch() reads gk_externs.h's thread-local jump buffers here. */
  CHECK(gk_sigtrap() == 1);
  switch (gk_sigcatch()) {
  case 0:
    gk_errexit(SIGERR, "expected error from the consumer test");
    break;
  case SIGERR:
    caught = 1;
    break;
  default:
    break;
  }
  CHECK(gk_siguntrap() == 1);
  CHECK(caught == 1);

  /* 2x3: row 0 = {0: 1, 2: 2}, row 1 = {1: 3, 2: 4}. */
  mat = gk_csr_Create();
  mat->nrows = 2;
  mat->ncols = 3;
  mat->rowptr = gk_zmalloc(3, "rowptr");
  mat->rowind = gk_imalloc(4, "rowind");
  mat->rowval = gk_fmalloc(4, "rowval");
  mat->rowptr[0] = 0;
  mat->rowptr[1] = 2;
  mat->rowptr[2] = 4;
  mat->rowind[0] = 0;
  mat->rowind[1] = 2;
  mat->rowind[2] = 1;
  mat->rowind[3] = 2;
  for (i = 0; i < 4; i++)
    mat->rowval[i] = (float)(i + 1);
  gk_csr_CreateIndex(mat, GK_CSR_COL);
  CHECK(mat->colptr[0] == 0 && mat->colptr[1] == 1 && mat->colptr[2] == 2 &&
        mat->colptr[3] == 4);
  CHECK(mat->colind[0] == 0 && mat->colind[1] == 1 && mat->colind[2] == 0 &&
        mat->colind[3] == 1);
  CHECK(mat->colval[0] == 1.0f && mat->colval[1] == 3.0f &&
        mat->colval[2] == 2.0f && mat->colval[3] == 4.0f);
  gk_csr_Free(&mat);
  CHECK(mat == NULL);

  return 0;
}
