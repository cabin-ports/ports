#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

int fail(const char *what) {
  std::fprintf(stderr, "pcre2: %s\n", what);
  return 1;
}

int allocations = 0;

extern "C" void *counting_malloc(PCRE2_SIZE size, void *) {
  ++allocations;
  return std::malloc(size);
}

extern "C" void counting_free(void *block, void *) { std::free(block); }

pcre2_code *compile(const char *pattern, pcre2_compile_context *ctx) {
  int err;
  PCRE2_SIZE erroff;
  return pcre2_compile(reinterpret_cast<PCRE2_SPTR>(pattern),
                       PCRE2_ZERO_TERMINATED, 0, &err, &erroff, ctx);
}

std::vector<std::string> years(const pcre2_code *re, const std::string &text) {
  std::vector<std::string> out;
  pcre2_match_data *md = pcre2_match_data_create_from_pattern(re, nullptr);
  PCRE2_SPTR subject = reinterpret_cast<PCRE2_SPTR>(text.data());
  PCRE2_SIZE start = 0;
  while (pcre2_match(re, subject, text.size(), start, 0, md, nullptr) > 0) {
    PCRE2_SIZE *ov = pcre2_get_ovector_pointer(md);
    out.push_back(text.substr(ov[2], ov[3] - ov[2]));
    start = ov[1];
  }
  pcre2_match_data_free(md);
  return out;
}

}  // namespace

int main() {
  pcre2_general_context *gctx =
      pcre2_general_context_create(counting_malloc, counting_free, nullptr);
  pcre2_compile_context *cctx = pcre2_compile_context_create(gctx);
  if (gctx == nullptr || cctx == nullptr) return fail("contexts");
  allocations = 0;
  pcre2_code *re = compile("(?<year>\\d{4})-(?<month>\\d{2})", cctx);
  if (re == nullptr) return fail("compile");
  if (allocations == 0) return fail("custom allocator unused");

  uint32_t captures = 0;
  uint32_t names = 0;
  pcre2_pattern_info(re, PCRE2_INFO_CAPTURECOUNT, &captures);
  pcre2_pattern_info(re, PCRE2_INFO_NAMECOUNT, &names);
  if (captures != 2 || names != 2) return fail("pattern info");

  const std::string text = "2024-01, 1999-12 and 2026-11";
  const std::vector<std::string> expected = {"2024", "1999", "2026"};
  if (years(re, text) != expected) return fail("iterated matches");

  // Serialize and decode the compiled pattern; the copy must match too.
  uint8_t *bytes = nullptr;
  PCRE2_SIZE size = 0;
  const pcre2_code *list[] = {re};
  if (pcre2_serialize_encode(list, 1, &bytes, &size, nullptr) != 1)
    return fail("serialize");
  pcre2_code *copy[1] = {nullptr};
  if (pcre2_serialize_decode(copy, 1, bytes, nullptr) != 1)
    return fail("deserialize");
  pcre2_serialize_free(bytes);

  pcre2_match_data *md = pcre2_match_data_create_from_pattern(copy[0], nullptr);
  if (pcre2_match(copy[0], reinterpret_cast<PCRE2_SPTR>("in 1987-06"),
                  PCRE2_ZERO_TERMINATED, 0, 0, md, nullptr) != 3)
    return fail("match with decoded pattern");
  PCRE2_UCHAR *month = nullptr;
  PCRE2_SIZE month_len = 0;
  if (pcre2_substring_get_byname(md, reinterpret_cast<PCRE2_SPTR>("month"),
                                 &month, &month_len) != 0 ||
      month_len != 2 || std::memcmp(month, "06", 2) != 0)
    return fail("named substring");
  pcre2_substring_free(month);
  pcre2_match_data_free(md);

  // A bad UTF-8 pattern is a compile error, not undefined behavior.
  int err = 0;
  PCRE2_SIZE erroff = 0;
  if (pcre2_compile(reinterpret_cast<PCRE2_SPTR>("\xff"), 1, PCRE2_UTF, &err,
                    &erroff, nullptr) != nullptr ||
      err != PCRE2_ERROR_UTF8_ERR21)
    return fail("invalid UTF-8 pattern");

  pcre2_code_free(copy[0]);
  pcre2_code_free(re);
  pcre2_compile_context_free(cctx);
  pcre2_general_context_free(gctx);
  std::puts("pcre2 ok");
  return 0;
}
