// C++ consumer: wide-character, case-insensitive and approximate matching
// through the legacy <tre/regex.h> names.
#include <tre/regex.h>
#include <tre/tre.h>

#include <cstdio>
#include <cstring>

static int failures;

static void check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "tre C++ test failed: %s\n", what);
    ++failures;
  }
}

int main() {
  regex_t re;
  regmatch_t m[2];

  // regcomp and regexec map to tre_regcomp and tre_regexec.
  check(regcomp(&re, "(x+)y", REG_EXTENDED | REG_ICASE) == REG_OK, "icase compile");
  check(regexec(&re, "aaXXYbb", 2, m, 0) == REG_OK, "icase match");
  check(m[0].rm_so == 2 && m[0].rm_eo == 5, "icase whole match");
  check(m[1].rm_so == 2 && m[1].rm_eo == 4, "icase group");
  regfree(&re);

  // Wide characters: offsets count wchar_t, not bytes.
  check(tre_regwcomp(&re, L"(あ+)い", REG_EXTENDED) == REG_OK, "wide compile");
  check(tre_regwexec(&re, L"zああいz", 2, m, 0) == REG_OK, "wide match");
  check(m[0].rm_so == 1 && m[0].rm_eo == 4, "wide whole match");
  check(m[1].rm_so == 1 && m[1].rm_eo == 3, "wide group");
  tre_regfree(&re);

  // Approximate matching reports its cost.
  regamatch_t am;
  regaparams_t params;
  regmatch_t pm[1];
  check(tre_regcomp(&re, "foobar", REG_EXTENDED) == REG_OK, "approx compile");
  tre_regaparams_default(&params);
  params.max_err = 1;
  std::memset(&am, 0, sizeof am);
  am.nmatch = 1;
  am.pmatch = pm;
  check(tre_regaexec(&re, "a fobar here", &am, params, 0) == REG_OK, "approx match");
  check(am.cost == 1 && am.num_del == 1 && pm[0].rm_so == 2 && pm[0].rm_eo == 7,
        "approx cost and span");
  params.max_err = 0;
  check(tre_regaexec(&re, "a fobar here", &am, params, 0) == REG_NOMATCH, "approx exact only");
  tre_regfree(&re);

  return failures != 0;
}
