/* Consumer test: the 8-, 16- and 32-bit libraries and the POSIX wrapper.
   Captures, named groups, UTF and Unicode properties, DFA matching, the
   match limit, error reporting, and the compiled-in configuration. */
#include <pcre.h>
#include <pcreposix.h>

#include <stdio.h>
#include <string.h>

static int failures = 0;

static void check(int ok, const char *what, int line)
{
  if (!ok) {
    fprintf(stderr, "main.c:%d: check failed: %s\n", line, what);
    ++failures;
  }
}
#define CHECK(cond) check((cond) ? 1 : 0, #cond, __LINE__)

static int config_int(int what)
{
  int value = -1;
  if (pcre_config(what, &value) != 0) return -2;
  return value;
}

static pcre *compile(const char *pattern, int options)
{
  const char *error = NULL;
  int offset = -1;
  pcre *re = pcre_compile(pattern, options, &error, &offset, NULL);
  if (re == NULL)
    fprintf(stderr, "pcre_compile(\"%s\") failed at %d: %s\n", pattern, offset,
            error);
  return re;
}

static void test_config(void)
{
  int value = -1;
  CHECK(strncmp(pcre_version(), "8.45 ", 5) == 0);
  CHECK(config_int(PCRE_CONFIG_UTF8) == 1);
  CHECK(config_int(PCRE_CONFIG_UNICODE_PROPERTIES) == 1);
  CHECK(config_int(PCRE_CONFIG_JIT) == 0);
  CHECK(config_int(PCRE_CONFIG_NEWLINE) == 10);
  CHECK(config_int(PCRE_CONFIG_LINK_SIZE) == 2);
  CHECK(config_int(PCRE_CONFIG_STACKRECURSE) == 1);
  CHECK(pcre16_config(PCRE_CONFIG_UTF16, &value) == 0 && value == 1);
  CHECK(pcre32_config(PCRE_CONFIG_UTF32, &value) == 0 && value == 1);
}

static void test_captures(void)
{
  const char *subject = "date: 2026-11-06!";
  int ovector[30];
  char year[8];
  const char *named = NULL;
  int rc;
  pcre *re = compile("(?<year>\\d{4})-(\\d{2})-(\\d{2})", 0);
  pcre_extra *extra;
  CHECK(re != NULL);
  if (re == NULL) return;
  extra = pcre_study(re, 0, &named);
  CHECK(named == NULL);
  rc = pcre_exec(re, extra, subject, (int)strlen(subject), 0, 0, ovector, 30);
  CHECK(rc == 4);
  CHECK(ovector[0] == 6 && ovector[1] == 16);
  CHECK(ovector[6] == 14 && ovector[7] == 16);
  CHECK(pcre_copy_substring(subject, ovector, rc, 1, year, sizeof year) == 4);
  CHECK(strcmp(year, "2026") == 0);
  CHECK(pcre_get_stringnumber(re, "year") == 1);
  named = NULL;
  CHECK(pcre_get_named_substring(re, subject, ovector, rc, "year", &named) ==
        4);
  CHECK(named != NULL && strcmp(named, "2026") == 0);
  if (named != NULL) pcre_free_substring(named);
  CHECK(pcre_exec(re, extra, "no date", 7, 0, 0, ovector, 30) ==
        PCRE_ERROR_NOMATCH);
  pcre_free_study(extra);
  pcre_free(re);
}

static void test_utf8(void)
{
  /* "abc " then Greek alpha, beta, gamma: two bytes each. */
  const char *greek = "abc \xce\xb1\xce\xb2\xce\xb3";
  /* Capital A with diaeresis, then "rger". */
  const char *word = "\xc3\x84rger";
  int ovector[6];
  pcre *re = compile("\\p{Greek}+", PCRE_UTF8);
  CHECK(re != NULL);
  if (re != NULL) {
    CHECK(pcre_exec(re, NULL, greek, (int)strlen(greek), 0, 0, ovector, 6) ==
          1);
    CHECK(ovector[0] == 4 && ovector[1] == 10);
    CHECK(pcre_exec(re, NULL, "\xce", 1, 0, 0, ovector, 6) ==
          PCRE_ERROR_BADUTF8);
    pcre_free(re);
  }
  /* \w covers non-ASCII letters only with PCRE_UCP. */
  re = compile("^\\w+$", PCRE_UTF8 | PCRE_UCP);
  CHECK(re != NULL);
  if (re != NULL) {
    CHECK(pcre_exec(re, NULL, word, (int)strlen(word), 0, 0, ovector, 6) == 1);
    pcre_free(re);
  }
  re = compile("^\\w+$", PCRE_UTF8);
  CHECK(re != NULL);
  if (re != NULL) {
    CHECK(pcre_exec(re, NULL, word, (int)strlen(word), 0, 0, ovector, 6) ==
          PCRE_ERROR_NOMATCH);
    pcre_free(re);
  }
}

static void test_dfa_and_limits(void)
{
  const char *subject = "aaaaaaaaaaaaaaab";
  int ovector[10];
  int workspace[64];
  pcre_extra extra;
  pcre *re = compile("foo|foobar", 0);
  CHECK(re != NULL);
  if (re != NULL) {
    /* The DFA matcher finds every alternative, longest first. */
    CHECK(pcre_dfa_exec(re, NULL, "foobar", 6, 0, 0, ovector, 10, workspace,
                        64) == 2);
    CHECK(ovector[0] == 0 && ovector[1] == 6);
    CHECK(ovector[2] == 0 && ovector[3] == 3);
    CHECK(pcre_exec(re, NULL, "foobar", 6, 0, 0, ovector, 10) == 1);
    CHECK(ovector[1] == 3);
    pcre_free(re);
  }
  re = compile("(a+)+$", 0);
  CHECK(re != NULL);
  if (re != NULL) {
    /* Under the default limit the backtracking runs out and fails; a
       caller's limit of 1000 stops it first. */
    CHECK(pcre_exec(re, NULL, subject, (int)strlen(subject), 0, 0, ovector,
                    10) == PCRE_ERROR_NOMATCH);
    memset(&extra, 0, sizeof extra);
    extra.flags = PCRE_EXTRA_MATCH_LIMIT;
    extra.match_limit = 1000;
    CHECK(pcre_exec(re, &extra, subject, (int)strlen(subject), 0, 0, ovector,
                    10) == PCRE_ERROR_MATCHLIMIT);
    pcre_free(re);
  }
}

static void test_errors(void)
{
  const char *error = NULL;
  int offset = -1;
  int code = 0;
  pcre *re = pcre_compile2("a(b", 0, &code, &error, &offset, NULL);
  CHECK(re == NULL);
  CHECK(error != NULL && strstr(error, "missing )") != NULL);
  CHECK(offset == 3);
  CHECK(code == 14);
}

static void test_pcre16(void)
{
  /* x, U+1F600 as a surrogate pair, y. */
  static const PCRE_UCHAR16 subject[] = {'x', 0xD83D, 0xDE00, 'y', 0};
  static const PCRE_UCHAR16 dot[] = {'x', '.', 'y', 0};
  const char *error = NULL;
  int offset = -1;
  int ovector[6];
  pcre16 *re = pcre16_compile(dot, PCRE_UTF16, &error, &offset, NULL);
  CHECK(re != NULL);
  if (re != NULL) {
    CHECK(pcre16_exec(re, NULL, subject, 4, 0, 0, ovector, 6) == 1);
    CHECK(ovector[0] == 0 && ovector[1] == 4);
    pcre16_free(re);
  }
  /* Without UTF-16, '.' matches a single surrogate. */
  re = pcre16_compile(dot, 0, &error, &offset, NULL);
  CHECK(re != NULL);
  if (re != NULL) {
    CHECK(pcre16_exec(re, NULL, subject, 4, 0, 0, ovector, 6) ==
          PCRE_ERROR_NOMATCH);
    pcre16_free(re);
  }
}

static void test_pcre32(void)
{
  static const PCRE_UCHAR32 pattern[] = {'\\', 'p', '{', 'H', 'a', 'n', '}',
                                         '+', 0};
  static const PCRE_UCHAR32 subject[] = {'a', 0x6F22, 0x5B57, 'b', 0};
  const char *error = NULL;
  int offset = -1;
  int ovector[6];
  pcre32 *re = pcre32_compile(pattern, PCRE_UTF32, &error, &offset, NULL);
  CHECK(re != NULL);
  if (re != NULL) {
    CHECK(pcre32_exec(re, NULL, subject, 4, 0, 0, ovector, 6) == 1);
    CHECK(ovector[0] == 1 && ovector[1] == 3);
    pcre32_free(re);
  }
}

static void test_posix(void)
{
  regex_t re;
  regmatch_t match[3];
  char message[128];
  /* \d is PCRE syntax: this is PCRE's regcomp, not the C library's. */
  CHECK(regcomp(&re, "^(\\w+)@(\\d+)\\.example$", REG_ICASE) == 0);
  CHECK(re.re_nsub == 2);
  CHECK(regexec(&re, "USER@42.EXAMPLE", 3, match, 0) == 0);
  CHECK(match[1].rm_so == 0 && match[1].rm_eo == 4);
  CHECK(match[2].rm_so == 5 && match[2].rm_eo == 7);
  CHECK(regexec(&re, "user@x.example", 3, match, 0) == REG_NOMATCH);
  regfree(&re);
  CHECK(regcomp(&re, "a(b", 0) != 0);
  CHECK(regerror(REG_EPAREN, NULL, message, sizeof message) > 1);
}

int main(void)
{
  test_config();
  test_captures();
  test_utf8();
  test_dfa_and_limits();
  test_errors();
  test_pcre16();
  test_pcre32();
  test_posix();
  if (failures != 0) {
    fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  puts("pcre consumer test passed");
  return 0;
}
