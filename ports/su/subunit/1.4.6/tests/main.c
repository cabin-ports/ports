#define _POSIX_C_SOURCE 200809L /* fileno */

#include <subunit/child.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <io.h>
#define dup _dup
#define dup2 _dup2
#define close _close
#define fileno _fileno
#else
#include <unistd.h>
#endif

static int failures = 0;

static void check(int ok, const char* what) {
  if (!ok) {
    fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

/* The API writes to stdout; capture it in a file among the probe's build
   outputs (MSVC creates tmpfile() files in the drive root). */
int main(void) {
  char buf[2048];
  size_t n;
  const char* dir = getenv("CABIN_PROFILE_DIR");
  char path[1024];
  FILE* in = NULL;
  int saved;
  const char* expected =
      "test: t1\n"
      "success: t1\n"
      "test: t2\n"
      "failure: t2 [\nbad value\n]\n"
      "error: t3 [\ncrashed\n]\n"
      "skip: t4 [\nno display\n]\n"
      "failure: t5 [\nline one\nline two\n]\n"
      "progress: 5\n"
      "progress: +2\n"
      "progress: -3\n"
      "progress: push\n"
      "progress: pop\n";

  if (dir != NULL && strlen(dir) < 900) {
    sprintf(path, "%s/port-probe-subunit.tmp", dir);
    in = fopen(path, "w+b");
  }
  fflush(stdout);
  saved = dup(fileno(stdout));
  if (saved < 0 || in == NULL || dup2(fileno(in), fileno(stdout)) < 0) {
    fprintf(stderr, "FAIL: cannot redirect stdout\n");
    return 1;
  }

  subunit_test_start("t1");
  subunit_test_pass("t1");
  subunit_test_start("t2");
  subunit_test_fail("t2", "bad value");
  subunit_test_error("t3", "crashed\n");
  subunit_test_skip("t4", "no display");
  subunit_test_fail("t5", "line one\nline two");
  subunit_progress(SUBUNIT_PROGRESS_SET, 5);
  subunit_progress(SUBUNIT_PROGRESS_CUR, 2);
  subunit_progress(SUBUNIT_PROGRESS_CUR, -3);
  subunit_progress(SUBUNIT_PROGRESS_PUSH, 0);
  subunit_progress(SUBUNIT_PROGRESS_POP, 0);

  fflush(stdout);
  dup2(saved, fileno(stdout));
  close(saved);

  rewind(in);
  n = fread(buf, 1, sizeof buf - 1, in);
  buf[n] = '\0';
  fclose(in);
  check(strcmp(buf, expected) == 0, "exact subunit v1 protocol bytes");
  if (strcmp(buf, expected) != 0) {
    fprintf(stderr, "got:\n%s", buf);
  }

  if (failures == 0) {
    puts("subunit: ok");
  }
  return failures == 0 ? 0 : 1;
}
