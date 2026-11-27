/* C89 consumer: POSIX-style and TRE extension entry points with exact offsets. */
#include <tre/tre.h>

#include <stdio.h>
#include <string.h>

static int failures;

static void check(int ok, const char *what) {
  if (!ok) {
    fprintf(stderr, "tre C test failed: %s\n", what);
    ++failures;
  }
}

int main(void) {
  regex_t re;
  regmatch_t m[3];
  char buf[64];
  int v;

  /* ERE submatch offsets. */
  check(tre_regcomp(&re, "a(b+)c", REG_EXTENDED) == REG_OK, "ere compile");
  check(re.re_nsub == 1, "ere nsub");
  check(tre_regexec(&re, "xxabbbc", 2, m, 0) == REG_OK, "ere match");
  check(m[0].rm_so == 2 && m[0].rm_eo == 7, "ere whole match");
  check(m[1].rm_so == 3 && m[1].rm_eo == 6, "ere group");
  check(tre_regexec(&re, "ac", 0, NULL, 0) == REG_NOMATCH, "ere nomatch");
  tre_regfree(&re);

  /* BRE back-reference, which selects the backtracking matcher. */
  check(tre_regcomp(&re, "\\(a*\\)b\\1", 0) == REG_OK, "bre compile");
  check(tre_have_backrefs(&re) == 1, "backrefs flag");
  check(tre_regexec(&re, "aabaa", 2, m, 0) == REG_OK, "bre match");
  check(m[0].rm_so == 0 && m[0].rm_eo == 5, "bre whole match");
  check(m[1].rm_so == 0 && m[1].rm_eo == 2, "bre group");
  tre_regfree(&re);

  /* Approximate matching: at most two errors. */
  check(tre_regcomp(&re, "(foobar){~2}", REG_EXTENDED) == REG_OK, "approx compile");
  check(tre_regexec(&re, "xirefoabrzlfd", 2, m, 0) == REG_OK, "approx match");
  check(m[0].rm_so == 4 && m[0].rm_eo == 9, "approx whole match");
  check(m[1].rm_so == 4 && m[1].rm_eo == 9, "approx group");
  check(tre_regexec(&re, "xirefoabzlfd", 0, NULL, 0) == REG_NOMATCH, "approx too far");
  tre_regfree(&re);

  /* A length-bounded pattern and subject may hold a NUL. */
  check(tre_regncomp(&re, "a.c", 3, 0) == REG_OK, "n compile");
  check(tre_regnexec(&re, "a\0c", 3, 0, NULL, 0) == REG_OK, "n exec with NUL");
  tre_regfree(&re);

  /* Errors and their messages. */
  check(tre_regcomp(&re, "(aaa", REG_EXTENDED) == REG_EPAREN, "unbalanced paren");
  check(tre_regerror(REG_EPAREN, NULL, buf, sizeof buf) == 12 &&
            strcmp(buf, "Missing ')'") == 0,
        "error message");

  /* Build configuration. */
  check(strcmp(tre_version(), "TRE 0.9.0 (BSD)") == 0, "version");
  check(tre_config(TRE_CONFIG_APPROX, &v) == REG_OK && v == 1, "config approx");
  check(tre_config(TRE_CONFIG_WCHAR, &v) == REG_OK && v == 1, "config wchar");

  return failures != 0;
}
