#include <cpp-subprocess/subprocess.hpp>

#include <cstdio>
#include <string>

namespace sp = subprocess;

#ifdef _WIN32
#define ECHO_HELLO {"cmd", "/c", "echo", "hello"}
#define EXIT_3 {"cmd", "/c", "exit", "3"}
#else
#define ECHO_HELLO {"echo", "hello"}
#define EXIT_3 {"sh", "-c", "exit 3"}
#endif

static int failures = 0;

static void check(bool ok, const char *what) {
  if (!ok) {
    std::fprintf(stderr, "FAILED: %s\n", what);
    ++failures;
  }
}

int main() {
  // check_output captures the child's stdout exactly.
  auto out = sp::check_output(ECHO_HELLO);
  std::string text(out.buf.begin(), out.buf.end());
#ifdef _WIN32
  // v2.1 appends a space to the command line, which cmd echoes, and the CRT
  // may translate "\r\n" to "\n"; drop both before the final newline.
  while (text.size() > 1 && (text[text.size() - 2] == '\r' || text[text.size() - 2] == ' '))
    text.erase(text.size() - 2, 1);
#endif
  check(text == "hello\n", "check_output stdout");

  // wait() returns the child's exit status.
  sp::Popen ok(ECHO_HELLO, sp::output{sp::PIPE});
  ok.communicate();
  check(ok.retcode() == 0, "zero exit status");

  sp::Popen p(EXIT_3);
  check(p.wait() == 3, "non-zero exit status");

  // check_output throws CalledProcessError carrying a failing status.
  bool thrown = false;
  try {
    sp::check_output(EXIT_3);
  } catch (sp::CalledProcessError &e) {
    thrown = e.retcode == 3;
  }
  check(thrown, "check_output CalledProcessError");

  return failures;
}
