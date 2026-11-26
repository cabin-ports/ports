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

static std::string text(const sp::OutBuffer &b) {
  return std::string(b.buf.begin(), b.buf.end());
}

int main() {
  // check_output captures the child's stdout exactly.
  std::string out = text(sp::check_output(ECHO_HELLO));
#ifdef _WIN32
  // cmd may echo a trailing space and the CRT may translate "\r\n".
  while (out.size() > 1 && (out[out.size() - 2] == '\r' || out[out.size() - 2] == ' '))
    out.erase(out.size() - 2, 1);
#endif
  check(out == "hello\n", "check_output stdout");

  // call() and wait() return the child's exit status.
  check(sp::call(ECHO_HELLO) == 0, "call zero status");
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

#ifndef _WIN32
  // communicate pipes input to the child and collects stdout and stderr.
  sp::Popen sh({"sh", "-c", "cat; echo err >&2"}, sp::input{sp::PIPE}, sp::output{sp::PIPE},
               sp::error{sp::PIPE});
  const std::string msg = "ping";
  auto res = sh.communicate(msg.c_str(), msg.size());
  check(text(res.first) == "ping", "communicate stdout");
  check(text(res.second) == "err\n", "communicate stderr");
  check(sh.retcode() == 0, "communicate retcode");
#endif

  return failures;
}
