/* C consumer, in C99: wai_getExecutablePath() and wai_getModulePath() report
 * the running executable (an absolute path to a file that starts with this
 * platform's executable magic and whose basename matches argv[0]'s), the
 * length-query call and the dirname length agree with the full call, the
 * module that contains main() is the executable itself, and a buffer that
 * is too small gets the required length and nothing past its capacity. */
#include <whereami.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

static void check(int ok, const char* what, int line) {
  if (!ok) {
    fprintf(stderr, "main.c:%d: check failed: %s\n", line, what);
    ++failures;
  }
}
#define CHECK(cond) check((cond), #cond, __LINE__)

static int is_separator(char c) {
#if defined(_WIN32)
  return c == '\\' || c == '/';
#else
  return c == '/';
#endif
}

static int is_absolute(const char* path) {
#if defined(_WIN32)
  return (isalpha((unsigned char)path[0]) && path[1] == ':' && path[2] == '\\') ||
         (path[0] == '\\' && path[1] == '\\');
#else
  return path[0] == '/';
#endif
}

static const char* basename_of(const char* path) {
  const char* base = path;
  for (; *path; ++path)
    if (is_separator(*path))
      base = path + 1;
  return base;
}

/* The basename's length without a trailing ".exe", which argv[0] may omit. */
static size_t stem_length(const char* base) {
  size_t n = strlen(base);
  if (n > 4 && base[n - 4] == '.' && tolower((unsigned char)base[n - 3]) == 'e' &&
      tolower((unsigned char)base[n - 2]) == 'x' && tolower((unsigned char)base[n - 1]) == 'e')
    n -= 4;
  return n;
}

static int same_stem(const char* a, const char* b) {
  size_t n = stem_length(a), i;
  if (n != stem_length(b))
    return 0;
  for (i = 0; i < n; ++i)
    if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i]))
      return 0;
  return 1;
}

/* The file starts with ELF, Mach-O (thin or universal) or PE magic. */
static int looks_executable(const char* path) {
  unsigned char magic[4] = {0, 0, 0, 0};
  FILE* f = fopen(path, "rb");
  size_t n;
  if (f == NULL)
    return 0;
  n = fread(magic, 1, sizeof magic, f);
  fclose(f);
  if (n < 2)
    return 0;
#if defined(_WIN32)
  return magic[0] == 'M' && magic[1] == 'Z';
#elif defined(__APPLE__)
  return n == 4 && ((magic[0] == 0xcf && magic[1] == 0xfa && magic[2] == 0xed && magic[3] == 0xfe) ||
                    (magic[0] == 0xca && magic[1] == 0xfe && magic[2] == 0xba && magic[3] == 0xbe));
#else
  return n == 4 && magic[0] == 0x7f && magic[1] == 'E' && magic[2] == 'L' && magic[3] == 'F';
#endif
}

int main(int argc, char** argv) {
  int length = wai_getExecutablePath(NULL, 0, NULL);
  int module_length = wai_getModulePath(NULL, 0, NULL);
  char* path;
  char* module;
  int dirname_length = -1, module_dirname = -1, i;
  char small[8];

  CHECK(length > 0);
  CHECK(module_length == length);
  if (length <= 0 || module_length != length) {
    fprintf(stderr, "whereami (C): FAILED\n");
    return 1;
  }

  path = (char*)malloc((size_t)length + 1);
  module = (char*)malloc((size_t)length + 1);
  CHECK(path != NULL && module != NULL);
  if (path == NULL || module == NULL)
    return 1;

  CHECK(wai_getExecutablePath(path, length, &dirname_length) == length);
  path[length] = '\0';
  CHECK(is_absolute(path));
  CHECK(dirname_length > 0 && dirname_length < length - 1);
  if (dirname_length <= 0 || dirname_length >= length - 1) {
    fprintf(stderr, "whereami (C): FAILED\n");
    return 1;
  }
  CHECK(is_separator(path[dirname_length]));
  for (i = dirname_length + 1; i < length; ++i)
    CHECK(!is_separator(path[i]));
  CHECK(looks_executable(path));
  if (argc > 0 && argv[0] != NULL && argv[0][0] != '\0')
    CHECK(same_stem(basename_of(path), basename_of(argv[0])));

  /* main() lives in the executable, so its module is the executable. */
  CHECK(wai_getModulePath(module, length, &module_dirname) == length);
  module[length] = '\0';
  CHECK(strcmp(module, path) == 0);
  CHECK(module_dirname == dirname_length);

  /* Too small: the required length comes back, dirname_length is left
   * alone, and nothing is written past the capacity. */
  memset(small, '#', sizeof small);
  dirname_length = -7;
  CHECK(wai_getExecutablePath(small, 3, &dirname_length) == length);
  CHECK(dirname_length == -7);
  for (i = 3; i < (int)sizeof small; ++i)
    CHECK(small[i] == '#');

  printf("whereami (C): %s (%s)\n", failures == 0 ? "ok" : "FAILED", path);
  free(module);
  free(path);
  return failures == 0 ? 0 : 1;
}
