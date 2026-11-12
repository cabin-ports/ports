/* Reads terminal descriptions from the TERMCAP environment variable and from
 * a termcap file with a tc= reference, then expands cursor motions with
 * tgoto and tparam and pads output with tputs. */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L /* setenv, mkstemp, fdopen */
#endif

#include <termcap.h>

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

static void check(int ok, const char *what) {
  if (!ok) {
    fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

static void set_env(const char *name, const char *value) {
#ifdef _WIN32
  _putenv_s(name, value);
#else
  setenv(name, value, 1);
#endif
}

static char output[256];
static size_t output_len;

static int collect(int c) {
  if (output_len + 1 < sizeof output) {
    output[output_len++] = (char)c;
    output[output_len] = '\0';
  }
  return c;
}

static void test_environment_entry(void) {
  char buf[2048];
  char area[64];
  char *ap = area;
  char *cl, *up, *cm, *pc;

  set_env("TERM", "cabin-test");
  set_env("TERMCAP", "cabin-test|Cabin test terminal:am:co#132:li#43:"
                     "cl=\\E[H\\E[J:cm=\\E[%i%d;%dH:up=^K:pc=@:");
  check(tgetent(buf, "cabin-test") == 1, "tgetent from TERMCAP");
  check(tgetnum("co") == 132 && tgetnum("li") == 43, "tgetnum");
  check(tgetnum("xx") == -1, "absent number");
  check(tgetflag("am") == 1 && tgetflag("bs") == 0, "tgetflag");
  check(tgetstr("co", &ap) == NULL, "a number is no string");

  cl = tgetstr("cl", &ap);
  check(cl == area && strcmp(cl, "\033[H\033[J") == 0, "\\E escapes");
  up = tgetstr("up", &ap);
  check(up == area + 7 && strcmp(up, "\013") == 0, "^ escapes");
  check(ap == area + 9, "the area pointer passes both strings");

  cm = tgetstr("cm", NULL);
  check(cm != NULL && strcmp(tgoto(cm, 4, 9), "\033[10;5H") == 0,
        "tgoto with %i");
  free(cm);

  pc = tgetstr("pc", &ap);
  PC = pc ? *pc : '\0';
  /* B9600 in termcap.c's speed table: upstream's arithmetic turns 20 ms of
   * padding into 18 pad characters. */
  ospeed = 13;
  output_len = 0;
  tputs("20xyz", 1, collect);
  check(strcmp(output, "xyz@@@@@@@@@@@@@@@@@@") == 0, "tputs pads with PC");
  /* With ospeed 0 there is no padding, but the delay prefix is consumed. */
  ospeed = 0;
  output_len = 0;
  tputs("5*ab", 3, collect);
  check(strcmp(output, "ab") == 0, "tputs without padding");
}

/* Not on Apple arm64, which passes the variadic arguments termcap.h
 * declares on the stack, where tparam's K&R definition does not read them
 * (cabin.toml's limitations). */
#if !(defined(__APPLE__) && defined(__aarch64__))
#define TEST_TPARAM 1
#endif

#ifdef TEST_TPARAM
static void test_tparam(void) {
  char small[4];
  char big[16];
  char *s;

  /* tparam.c defines tparam with four int parameters, so each call passes
   * four.  Too small a buffer makes tparam return a malloc'd one. */
  s = tparam("%d,%d", small, (int)sizeof small, 3, 41, 0, 0);
  check(s != NULL && s != small && strcmp(s, "3,41") == 0, "tparam grows");
  if (s != small) {
    free(s);
  }
  s = tparam("%2/%3", big, (int)sizeof big, 7, 1234, 0, 0);
  check(s == big && strcmp(s, "07/1234") == 0, "tparam %2 and %3");
}
#endif

/* Creates the database and leaves in PATH the name TERMCAP gets, which
 * must start with "/" to name a file: on Windows, the drive-relative name
 * of a file in the current directory. */
static FILE *create_termcap_file(char *path, size_t size) {
#ifdef _WIN32
  char cwd[1024];
  char name[] = "cabin-termcap-XXXXXX";
  char *p;

  if (_getcwd(cwd, sizeof cwd) == NULL || cwd[1] != ':' ||
      _mktemp_s(name, sizeof name) != 0) {
    return NULL;
  }
  snprintf(path, size, "%s\\%s", cwd + 2, name);
  for (p = path; *p != '\0'; ++p) {
    if (*p == '\\') {
      *p = '/';
    }
  }
  return fopen(path, "w");
#else
  const char *dir = getenv("TMPDIR");
  int fd;

  if (dir == NULL || *dir != '/') {
    dir = "/tmp";
  }
  snprintf(path, size, "%s/cabin-termcap-XXXXXX", dir);
  fd = mkstemp(path);
  return fd < 0 ? NULL : fdopen(fd, "w");
#endif
}

static void test_file(void) {
  char path[1024];
  char buf[2048];
  char area[64];
  char *ap = area;
  char *cl;
  FILE *f = create_termcap_file(path, sizeof path);

  check(f != NULL, "create the termcap file");
  if (f == NULL) {
    return;
  }
  /* The first line would match cabin-alias but for its "#". */
  fputs("#x|cabin-alias|commented out:co#1:\n"
        "other|another terminal:co#40:\n"
        "cabin-base|base entry:am:co#72:li#24:\\\n"
        "\t:cl=\\E[H\\E[J:\n"
        "cabin-file|cabin-alias|file terminal:co#100:tc=cabin-base:\n",
        f);
  fclose(f);

  set_env("TERM", "other");
  set_env("TERMCAP", path);
  check(tgetent(buf, "cabin-alias") == 1, "tgetent from a file");
  check(tgetnum("co") == 100, "the entry's number over its tc= target's");
  check(tgetnum("li") == 24 && tgetflag("am"), "capabilities through tc=");
  cl = tgetstr("cl", &ap);
  check(cl != NULL && strcmp(cl, "\033[H\033[J") == 0,
        "a capability on a continuation line");
  check(tgetent(buf, "absent") == 0, "an absent type");
  remove(path);

  set_env("TERMCAP", "/nonexistent-cabin-termcap-dir/termcap");
  check(tgetent(buf, "cabin-alias") == -1, "an unreadable file");
}

int main(void) {
  test_environment_entry();
#ifdef TEST_TPARAM
  test_tparam();
#endif
  test_file();
  if (failures == 0) {
    puts("termcap: ok");
  }
  return failures == 0 ? 0 : 1;
}
