// Consumer test from C++ (pcrecpp is not ported): the C API through
// pcre.h's extern "C" block, a global-match loop with pcre_fullinfo, and
// the POSIX wrapper.
#include <pcre.h>
#include <pcreposix.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char* what, int line) {
  if (!ok) {
    std::fprintf(stderr, "main.cc:%d: check failed: %s\n", line, what);
    ++failures;
  }
}
#define CHECK(cond) check((cond), #cond, __LINE__)

std::vector<std::string> find_all(const char* pattern, const std::string& text) {
  std::vector<std::string> found;
  const char* error = 0;
  int offset = -1;
  pcre* re = pcre_compile(pattern, PCRE_UTF8, &error, &offset, 0);
  if (re == 0) {
    std::fprintf(stderr, "pcre_compile failed at %d: %s\n", offset, error);
    ++failures;
    return found;
  }
  int captures = -1;
  CHECK(pcre_fullinfo(re, 0, PCRE_INFO_CAPTURECOUNT, &captures) == 0);
  CHECK(captures == 1);
  int ovector[6];
  int start = 0;
  while (start <= static_cast<int>(text.size()) &&
         pcre_exec(re, 0, text.data(), static_cast<int>(text.size()), start, 0,
                   ovector, 6) == 2) {
    found.push_back(text.substr(ovector[2], ovector[3] - ovector[2]));
    start = ovector[1] > ovector[0] ? ovector[1] : ovector[1] + 1;
  }
  pcre_free(re);
  return found;
}

}  // namespace

int main() {
  std::vector<std::string> keys =
      find_all("(\\w+)=", "alpha=1, beta=22; gamma=333");
  CHECK(keys.size() == 3);
  CHECK(keys.size() == 3 && keys[0] == "alpha" && keys[1] == "beta" &&
        keys[2] == "gamma");

  // Empty matches advance one position at a time.
  CHECK(find_all("(x*)", "ab").size() == 3);

  regex_t re;
  regmatch_t match[2];
  CHECK(regcomp(&re, "(\\d+)$", 0) == 0);
  CHECK(regexec(&re, "order 1234", 2, match, 0) == 0);
  CHECK(match[1].rm_so == 6 && match[1].rm_eo == 10);
  regfree(&re);

  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("pcre C++ consumer test passed");
  return 0;
}
