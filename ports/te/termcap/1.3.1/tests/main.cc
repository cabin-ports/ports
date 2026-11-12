// Uses termcap from C++, wrapping its header in extern "C" as it has none:
// an entry from the TERMCAP environment variable, tgoto, and tputs writing
// through a C++ function.  C++98, as the probe compiles it.
extern "C" {
#include <termcap.h>
}

#include <cstdio>
#include <cstdlib>
#include <string>

static int failures = 0;

static void check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

static void set_env(const char* name, const char* value) {
#ifdef _WIN32
  _putenv_s(name, value);
#else
  setenv(name, value, 1);
#endif
}

static std::string output;

static int append(int c) {
  output += static_cast<char>(c);
  return c;
}

int main() {
  set_env("TERM", "cabin-cxx");
  set_env("TERMCAP", "cabin-cxx|vt52-like:co#80:li#24:cm=\\EY%+ %+ :");
  char buf[2048];
  check(tgetent(buf, "cabin-cxx") == 1, "tgetent from TERMCAP");
  check(tgetnum("co") == 80 && tgetnum("li") == 24, "tgetnum");

  char area[32];
  char* ap = area;
  const char* cm = tgetstr("cm", &ap);
  check(cm != NULL, "tgetstr");
  if (cm != NULL) {
    // %+ adds the character code of the space: row 5, column 9.
    check(std::string(tgoto(cm, 9, 5)) == "\033Y%)", "tgoto with %+");
  }

  ospeed = 0;
  tputs("hello", 1, append);
  check(output == "hello", "tputs through a C++ function");

  if (failures == 0) {
    std::puts("termcap: ok");
  }
  return failures == 0 ? 0 : 1;
}
