#include <cargs.h>

#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstring>

namespace {

int failures = 0;
const std::size_t kTextSize = 512;

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__,  \
                   #cond);                                                   \
      ++failures;                                                            \
    }                                                                        \
  } while (0)

// Collects what cag_option_printer prints.
int collect(void *ctx, const char *fmt, ...) {
  char *out = static_cast<char *>(ctx);
  std::va_list ap;
  va_start(ap, fmt);
  const std::size_t used = std::strlen(out);
  const int n = std::vsnprintf(out + used, kTextSize - used, fmt, ap);
  va_end(ap);
  return n;
}

}  // namespace

int main() {
  const cag_option options[] = {
      {'v', "v", "verbose", NULL, "Verbose output"},
      {'o', "o", "output", "FILE", "Output file"}};

  char a0[] = "test", a1[] = "--verbose", a2[] = "in.txt", a3[] = "--output",
       a4[] = "out.txt", a5[] = "-x";
  char *argv[] = {a0, a1, a2, a3, a4, a5};

  cag_option_context ctx;
  cag_option_init(&ctx, options, CAG_ARRAY_SIZE(options), 6, argv);

  CHECK(cag_option_fetch(&ctx));
  CHECK(cag_option_get_identifier(&ctx) == 'v');
  CHECK(cag_option_fetch(&ctx));
  CHECK(cag_option_get_identifier(&ctx) == 'o');
  CHECK(std::strcmp(cag_option_get_value(&ctx), "out.txt") == 0);
  // "-x" is unknown: reported as '?' with its argv index and letter.
  CHECK(cag_option_fetch(&ctx));
  CHECK(cag_option_get_identifier(&ctx) == '?');
  CHECK(std::strcmp(argv[cag_option_get_error_index(&ctx)], "-x") == 0);
  CHECK(cag_option_get_error_letter(&ctx) == 'x');
  CHECK(!cag_option_fetch(&ctx));
  CHECK(std::strcmp(argv[cag_option_get_index(&ctx)], "in.txt") == 0);

  char text[kTextSize] = "";
  cag_option_printer(options, CAG_ARRAY_SIZE(options), collect, text);
  CHECK(std::strstr(text, "-v, --verbose") != NULL);
  CHECK(std::strstr(text, "-o, --output=FILE") != NULL);
  CHECK(std::strstr(text, "Output file") != NULL);

  return failures ? 1 : 0;
}
