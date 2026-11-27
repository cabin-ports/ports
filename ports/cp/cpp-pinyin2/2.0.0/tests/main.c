#include <cpp-pinyin2/ext/c_api.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#define RMDIR(p) _rmdir(p)
#define TMPVAR "TEMP"
#define TMPDEF "."
#else
#include <sys/stat.h>
#include <unistd.h>
#define MKDIR(p) mkdir((p), 0700)
#define RMDIR(p) rmdir(p)
#define TMPVAR "TMPDIR"
#define TMPDEF "/tmp"
#endif

static int failures = 0;

#define CHECK(cond)                                         \
  do {                                                      \
    if (!(cond)) {                                          \
      printf("FAIL line %d: %s\n", __LINE__, #cond);        \
      ++failures;                                           \
    }                                                       \
  } while (0)

static const char *const files[] = {"format.txt", "word.txt", "phrases_dict.txt", "phrases_map.txt",
                                    "trans_word.txt"};
static const char *const texts[] = {
    "2\n",
    "\xe4\xb8\xad:zh\xc5\x8dng,zh\xc3\xb2ng\n"
    "\xe5\x9b\xbd:gu\xc3\xb3\n"
    "\xe5\xa5\x96:ji\xc7\x8eng\n",
    "\xe4\xb8\xad\xe5\xa5\x96:zh\xc3\xb2ng,ji\xc7\x8eng\n",
    "\xe4\xb8\xad:2\n",
    "\xe5\x9c\x8b:\xe5\x9b\xbd\n"};
enum { NFILES = sizeof files / sizeof files[0] };

static char root[1024], sub[1100];

static void path_of(char *out, size_t n, const char *dir, const char *name) {
  snprintf(out, n, "%s/%s", dir, name);
}

static void write_dict(void) {
  int i;
  char p[1200];
  FILE *f;
  MKDIR(root);
  MKDIR(sub);
  for (i = 0; i < NFILES; ++i) {
    path_of(p, sizeof p, sub, files[i]);
    f = fopen(p, "wb");
    if (f) {
      fputs(texts[i], f);
      fclose(f);
    }
  }
}

static void remove_dict(void) {
  int i;
  char p[1200];
  for (i = 0; i < NFILES; ++i) {
    path_of(p, sizeof p, sub, files[i]);
    remove(p);
  }
  RMDIR(sub);
  RMDIR(root);
}

/* Renders every segment of `text` in TONE3, space separated, into `out`. */
static int render(const cppp2_engine *e, const char *text, char *out, size_t n) {
  cppp2_query_opts opts;
  cppp2_result *r = NULL;
  size_t i;
  int32_t st;
  cppp2_query_opts_init(&opts);
  opts.style = 8; /* MandarinStyle::TONE3 */
  st = cppp2_query(e, text, &opts, &r);
  out[0] = '\0';
  if (st != CPPP2_OK) return st;
  for (i = 0; i < r->count; ++i) {
    if (i) strncat(out, " ", n - strlen(out) - 1);
    strncat(out, r->segments[i].rendered, n - strlen(out) - 1);
  }
  cppp2_result_free(r);
  return CPPP2_OK;
}

int main(void) {
  const char *tmp = getenv(TMPVAR);
  cppp2_error err;
  cppp2_engine *engine;
  char out[256];
  char **readings = NULL;
  size_t count = 0;
  char *simplified = NULL;

  CHECK(strcmp(cppp2_version(), "2.0.0") == 0);
  CHECK(strcmp(cppp2_code_text(CPPP2_OK), "") != 0);

  snprintf(root, sizeof root, "%s/cabin-cpp-pinyin2-c", tmp && *tmp ? tmp : TMPDEF);
  snprintf(sub, sizeof sub, "%s/mandarin", root);
  write_dict();

  engine = cppp2_open(NULL, "mandarin", root, &err);
  CHECK(engine != NULL && err.code == CPPP2_OK);
  if (engine) {
    CHECK(render(engine, "\xe4\xb8\xad\xe5\x9b\xbd", out, sizeof out) == CPPP2_OK &&
          strcmp(out, "zhong1 guo2") == 0);
    CHECK(render(engine, "\xe4\xb8\xad\xe5\xa5\x96", out, sizeof out) == CPPP2_OK &&
          strcmp(out, "zhong4 jiang3") == 0);

    {
      cppp2_query_opts opts;
      cppp2_query_opts_init(&opts);
      opts.style = 8;
      CHECK(cppp2_readings(engine, "\xe4\xb8\xad", &opts, &readings, &count) == CPPP2_OK);
      CHECK(count == 2 && strcmp(readings[0], "zhong1") == 0 && strcmp(readings[1], "zhong4") == 0);
      cppp2_string_list_free(readings, count);
    }
    CHECK(cppp2_to_simplified(engine, "\xe5\x9c\x8b", &simplified) == CPPP2_OK &&
          strcmp(simplified, "\xe5\x9b\xbd") == 0);
    cppp2_string_free(simplified);
    cppp2_destroy(engine);
  }
  remove_dict();

  /* The directory is gone now: opening it is a Load error. */
  engine = cppp2_open(NULL, "mandarin", root, &err);
  CHECK(engine == NULL && err.code == CPPP2_ERR_LOAD);
  cppp2_string_free(err.detail);
  return failures == 0 ? 0 : 1;
}
