/* PCRE2_CODE_UNIT_WIDTH 0 declares the 8-, 16- and 32-bit APIs side by
   side, so one program exercises all three libraries. */
#define PCRE2_CODE_UNIT_WIDTH 0
#include <pcre2.h>
#include <pcre2posix.h>

#include <stdio.h>
#include <string.h>

#if PCRE2_MAJOR != 10 || PCRE2_MINOR != 49
#error unexpected PCRE2 version
#endif

static int fail(const char *what) {
  fprintf(stderr, "pcre2: %s\n", what);
  return 1;
}

static uint32_t config_u32(uint32_t what) {
  uint32_t value = 0xffffffffu;
  pcre2_config_8(what, &value);
  return value;
}

static int check_config(void) {
  char version[32];
  uint32_t widths16 = 0;
  uint32_t widths32 = 0;
  if (pcre2_config_8(PCRE2_CONFIG_VERSION, version) < 0 ||
      strncmp(version, "10.49 ", 6) != 0)
    return fail("version string");
  if (config_u32(PCRE2_CONFIG_UNICODE) != 1) return fail("Unicode support");
  if (config_u32(PCRE2_CONFIG_JIT) != 0) return fail("JIT unexpectedly on");
  if (config_u32(PCRE2_CONFIG_NEWLINE) != PCRE2_NEWLINE_LF)
    return fail("default newline");
  if (config_u32(PCRE2_CONFIG_LINKSIZE) != 2) return fail("link size");
  pcre2_config_16(PCRE2_CONFIG_COMPILED_WIDTHS, &widths16);
  pcre2_config_32(PCRE2_CONFIG_COMPILED_WIDTHS, &widths32);
  if (config_u32(PCRE2_CONFIG_COMPILED_WIDTHS) != 7 || widths16 != 7 ||
      widths32 != 7)
    return fail("compiled widths");
  return 0;
}

/* \p{Lu} needs Unicode support; with UCP, \w matches non-ASCII letters. */
static int check_utf8(void) {
  /* "café Ärger" */
  static const char subject[] = "caf\xC3\xA9 \xC3\x84rger";
  int err;
  PCRE2_SIZE erroff;
  PCRE2_SIZE *ov;
  PCRE2_UCHAR8 word[16];
  PCRE2_SIZE word_len = sizeof word;
  pcre2_match_data_8 *md;
  int rc;
  pcre2_code_8 *re = pcre2_compile_8(
      (PCRE2_SPTR8) "(?<word>\\p{Lu}\\w+)", PCRE2_ZERO_TERMINATED,
      PCRE2_UTF | PCRE2_UCP, &err, &erroff, NULL);
  if (re == NULL) return fail("compile UTF-8 pattern");
  md = pcre2_match_data_create_from_pattern_8(re, NULL);
  rc = pcre2_match_8(re, (PCRE2_SPTR8)subject, PCRE2_ZERO_TERMINATED, 0, 0,
                     md, NULL);
  ov = pcre2_get_ovector_pointer_8(md);
  if (rc != 2 || ov[0] != 6 || ov[1] != 12) return fail("UTF-8 match offsets");
  if (pcre2_substring_copy_byname_8(md, (PCRE2_SPTR8) "word", word,
                                    &word_len) != 0 ||
      word_len != 6 || memcmp(word, "\xC3\x84rger", 6) != 0)
    return fail("named substring");
  /* The second byte of a two-byte sequence must be 10xxxxxx. */
  rc = pcre2_match_8(re, (PCRE2_SPTR8) "\xC3(", 2, 0, 0, md, NULL);
  if (rc != PCRE2_ERROR_UTF8_ERR6) return fail("invalid UTF-8 not rejected");
  pcre2_match_data_free_8(md);
  pcre2_code_free_8(re);
  return 0;
}

static int check_substitute(void) {
  int err;
  PCRE2_SIZE erroff;
  PCRE2_UCHAR8 out[32];
  PCRE2_SIZE out_len = sizeof out;
  int rc;
  pcre2_code_8 *re = pcre2_compile_8((PCRE2_SPTR8) "a+", PCRE2_ZERO_TERMINATED,
                                     0, &err, &erroff, NULL);
  if (re == NULL) return fail("compile substitute pattern");
  rc = pcre2_substitute_8(re, (PCRE2_SPTR8) "caaab aa", PCRE2_ZERO_TERMINATED,
                          0, PCRE2_SUBSTITUTE_GLOBAL, NULL, NULL,
                          (PCRE2_SPTR8) "<$0>", PCRE2_ZERO_TERMINATED, out,
                          &out_len);
  if (rc != 2 || out_len != 12 || memcmp(out, "c<aaa>b <aa>", 12) != 0)
    return fail("global substitution");
  pcre2_code_free_8(re);
  return 0;
}

/* The DFA matcher reports every match at the start, longest first. */
static int check_dfa(void) {
  int err;
  PCRE2_SIZE erroff;
  int workspace[64];
  PCRE2_SIZE *ov;
  int rc;
  pcre2_match_data_8 *md = pcre2_match_data_create_8(4, NULL);
  pcre2_code_8 *re = pcre2_compile_8((PCRE2_SPTR8) "<.*>", PCRE2_ZERO_TERMINATED,
                                     0, &err, &erroff, NULL);
  if (re == NULL || md == NULL) return fail("compile DFA pattern");
  rc = pcre2_dfa_match_8(re, (PCRE2_SPTR8) "<a><b>", PCRE2_ZERO_TERMINATED, 0,
                         0, md, NULL, workspace, 64);
  ov = pcre2_get_ovector_pointer_8(md);
  if (rc != 2 || ov[0] != 0 || ov[1] != 6 || ov[2] != 0 || ov[3] != 3)
    return fail("DFA matches");
  pcre2_match_data_free_8(md);
  pcre2_code_free_8(re);
  return 0;
}

static int check_compile_error(void) {
  int err = 0;
  PCRE2_SIZE erroff = 0;
  PCRE2_UCHAR8 message[128];
  if (pcre2_compile_8((PCRE2_SPTR8) "(", PCRE2_ZERO_TERMINATED, 0, &err,
                      &erroff, NULL) != NULL)
    return fail("unbalanced pattern compiled");
  if (err != PCRE2_ERROR_MISSING_CLOSING_PARENTHESIS || erroff != 1)
    return fail("compile error code");
  if (pcre2_get_error_message_8(err, message, sizeof message) < 0 ||
      strcmp((const char *)message, "missing closing parenthesis") != 0)
    return fail("compile error message");
  return 0;
}

/* U+1F600 is one code unit in 32-bit mode and a surrogate pair in 16-bit
   mode. */
static int check_utf16(void) {
  static const PCRE2_UCHAR16 pattern[] = {'\\', 'x', '{', '1', 'F', '6',
                                          '0', '0', '}', 0};
  static const PCRE2_UCHAR16 subject[] = {'a', 0xD83D, 0xDE00, 'b'};
  static const PCRE2_UCHAR16 broken[] = {0xD83D, 'b'};
  int err;
  PCRE2_SIZE erroff;
  PCRE2_SIZE *ov;
  int rc;
  pcre2_match_data_16 *md;
  pcre2_code_16 *re = pcre2_compile_16(pattern, PCRE2_ZERO_TERMINATED,
                                       PCRE2_UTF, &err, &erroff, NULL);
  if (re == NULL) return fail("compile UTF-16 pattern");
  md = pcre2_match_data_create_from_pattern_16(re, NULL);
  rc = pcre2_match_16(re, subject, 4, 0, 0, md, NULL);
  ov = pcre2_get_ovector_pointer_16(md);
  if (rc != 1 || ov[0] != 1 || ov[1] != 3) return fail("UTF-16 match offsets");
  if (pcre2_match_16(re, broken, 2, 0, 0, md, NULL) != PCRE2_ERROR_UTF16_ERR2)
    return fail("unpaired surrogate not rejected");
  pcre2_match_data_free_16(md);
  pcre2_code_free_16(re);
  return 0;
}

static int check_utf32(void) {
  static const PCRE2_UCHAR32 pattern[] = {'\\', 'x', '{', '1', 'F', '6', '0',
                                          '0', '}', '\\', 'p', '{', 'L', 'u',
                                          '}', 0};
  /* 'x', U+1F600, GREEK CAPITAL LETTER ALPHA, 'y' */
  static const PCRE2_UCHAR32 subject[] = {'x', 0x1F600, 0x0391, 'y'};
  int err;
  PCRE2_SIZE erroff;
  PCRE2_SIZE *ov;
  int rc;
  pcre2_match_data_32 *md;
  pcre2_code_32 *re = pcre2_compile_32(pattern, PCRE2_ZERO_TERMINATED,
                                       PCRE2_UTF, &err, &erroff, NULL);
  if (re == NULL) return fail("compile UTF-32 pattern");
  md = pcre2_match_data_create_from_pattern_32(re, NULL);
  rc = pcre2_match_32(re, subject, 4, 0, 0, md, NULL);
  ov = pcre2_get_ovector_pointer_32(md);
  if (rc != 1 || ov[0] != 1 || ov[1] != 3) return fail("UTF-32 match offsets");
  pcre2_match_data_free_32(md);
  pcre2_code_free_32(re);
  return 0;
}

static int check_posix(void) {
  regex_t re;
  regex_t bad;
  regmatch_t m[3];
  char message[128];
  int rc;
  if (regcomp(&re, "([0-9]+)-([0-9]+)", REG_EXTENDED) != 0)
    return fail("regcomp");
  if (re.re_nsub != 2) return fail("regcomp subexpression count");
  if (regexec(&re, "tel 12-345", 3, m, 0) != 0) return fail("regexec");
  if (m[0].rm_so != 4 || m[0].rm_eo != 10 || m[1].rm_so != 4 ||
      m[1].rm_eo != 6 || m[2].rm_so != 7 || m[2].rm_eo != 10)
    return fail("regexec offsets");
  if (regexec(&re, "no digits", 3, m, 0) != REG_NOMATCH)
    return fail("regexec without a match");
  regfree(&re);
  rc = regcomp(&bad, "(", 0);
  if (rc == 0) return fail("regcomp accepted an unbalanced pattern");
  if (regerror(rc, &bad, message, sizeof message) == 0 || message[0] == '\0')
    return fail("regerror");
  return 0;
}

int main(void) {
  if (check_config() || check_utf8() || check_substitute() || check_dfa() ||
      check_compile_error() || check_utf16() || check_utf32() ||
      check_posix())
    return 1;
  puts("pcre2 ok");
  return 0;
}
