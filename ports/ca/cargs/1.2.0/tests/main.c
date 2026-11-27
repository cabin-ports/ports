#include <cargs.h>
#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); \
      ++failures;                                                            \
    }                                                                        \
  } while (0)

int main(void)
{
  static const cag_option options[] = {
    {.identifier = 's', .access_letters = "s", .description = "Simple flag"},
    {.identifier = 'a', .access_letters = "a", .description = "Another flag"},
    {.identifier = 'k', .access_letters = "k", .access_name = "key",
      .value_name = "VALUE", .description = "Parameter value"}};

  char a0[] = "test", a1[] = "-s", a2[] = "in1", a3[] = "-k=v42",
       a4[] = "in2", a5[] = "-a", a6[] = "in3", a7[] = "in4",
       a8[] = "--", a9[] = "-q", a10[] = "-z", a11[] = "in5";
  char *argv[] = {a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11};
  const int argc = 12;
  const char *rest[] = {"in1", "in2", "in3", "in4", "--", "-q",
    "-z", "in5"};
  cag_option_context ctx;
  int simple = 0, another = 0, key = 0, other = 0, i;
  const char *value = NULL;

  cag_option_init(&ctx, options, CAG_ARRAY_SIZE(options), argc, argv);
  while (cag_option_fetch(&ctx)) {
    switch (cag_option_get_identifier(&ctx)) {
    case 's': ++simple; break;
    case 'a': ++another; break;
    case 'k': ++key; value = cag_option_get_value(&ctx); break;
    default: ++other; break;
    }
  }

  CHECK(simple == 1 && another == 1 && key == 1 && other == 0);
  CHECK(value != NULL && strcmp(value, "v42") == 0);
  CHECK(cag_option_get_error_index(&ctx) == -1);
  /* Non-option arguments are moved behind the options, after the index. */
  CHECK(cag_option_get_index(&ctx) == argc - 8);
  for (i = 0; i < 8; ++i) {
    CHECK(strcmp(argv[cag_option_get_index(&ctx) + i], rest[i]) == 0);
  }

  return failures ? 1 : 0;
}
