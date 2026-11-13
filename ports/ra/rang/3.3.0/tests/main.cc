// A C++11 consumer of rang (a C++-only library): checks the exact escape
// bytes written in each control mode and Windows terminal mode, to a string
// stream and to std::cout with a string buffer swapped in, and prints
// whether this host's stdout is a terminal or a Windows console.
#include "rang.hpp"

#include <cstdio>
#include <iostream>
#include <sstream>
#include <string>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

namespace {

int failures = 0;

#define CHECK(cond)                                               \
  do {                                                            \
    if (!(cond)) {                                                \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      ++failures;                                                 \
    }                                                             \
  } while (0)

std::string sample() {
  std::ostringstream os;
  os << rang::style::bold << rang::fg::red << "error" << rang::style::reset
     << ' ' << rang::bg::blue << rang::fgB::yellow << 42 << rang::bgB::cyan
     << rang::fg::reset << rang::bg::reset << rang::style::underline << '!'
     << rang::style::reset;
  return os.str();
}

const char kPlain[] = "error 42!";
const char kAnsi[] =
    "\033[1m\033[31merror\033[0m \033[44m\033[93m42\033[106m\033[39m"
    "\033[49m\033[4m!\033[0m";

template <class T>
std::string written(T value) {
  std::ostringstream os;
  os << value;
  return os.str();
}

// rang recognises std::cout by its current buffer, so a swapped-in string
// buffer takes the std::cout branches without writing to the real stdout.
std::string via_cout() {
  std::stringbuf buffer;
  std::streambuf* const saved = std::cout.rdbuf(&buffer);
  std::cout << rang::fg::red << 'x' << rang::style::reset;
  std::cout.rdbuf(saved);
  return buffer.str();
}

}  // namespace

int main() {
  // The defaults are control::Auto and winTerm::Auto, and a string stream is
  // never a terminal, so no codes are written.
  CHECK(sample() == kPlain);

  // Forced ANSI output is the same on every platform.
  rang::setControlMode(rang::control::Force);
  rang::setWinTermMode(rang::winTerm::Ansi);
  CHECK(sample() == kAnsi);
  CHECK(written(rang::style::italic) == "\033[3m");
  CHECK(written(rang::style::crossed) == "\033[9m");
  CHECK(written(rang::fg::black) == "\033[30m");
  CHECK(written(rang::fg::gray) == "\033[37m");
  CHECK(written(rang::bg::gray) == "\033[47m");
  CHECK(written(rang::fgB::black) == "\033[90m");
  CHECK(written(rang::bgB::gray) == "\033[107m");

  // Each mode change starts from forced ANSI output, so a change that did
  // not take effect would show.
  rang::setControlMode(rang::control::Off);
  CHECK(sample() == kPlain);
  rang::setControlMode(rang::control::Force);
  CHECK(sample() == kAnsi);
  rang::setControlMode(rang::control::Auto);
  CHECK(sample() == kPlain);
  rang::setControlMode(rang::control::Force);

  // The winTerm mode only matters on Windows, where Native, and Auto for a
  // buffer not shared with cout, cerr or clog, use the console API, which
  // has no handle for a string stream, so nothing is written.
#ifdef _WIN32
  const std::string forced_native = kPlain;
#else
  const std::string forced_native = kAnsi;
#endif
  rang::setWinTermMode(rang::winTerm::Native);
  CHECK(sample() == forced_native);
  rang::setWinTermMode(rang::winTerm::Ansi);
  CHECK(sample() == kAnsi);
  rang::setWinTermMode(rang::winTerm::Auto);
  CHECK(sample() == forced_native);

  rang::setWinTermMode(rang::winTerm::Ansi);
  CHECK(via_cout() == "\033[31mx\033[0m");
  // On Windows the native mode sets the attributes of the real stdout
  // handle (which fails harmlessly for a pipe or file) and writes nothing.
  rang::setWinTermMode(rang::winTerm::Native);
#ifdef _WIN32
  CHECK(via_cout() == "x");
#else
  CHECK(via_cout() == "\033[31mx\033[0m");
  // Auto mode colours std::cout only when stdout is a terminal.
  if (!isatty(fileno(stdout))) {
    rang::setControlMode(rang::control::Auto);
    CHECK(via_cout() == "x");
  }
#endif

#ifdef _WIN32
  DWORD console_mode = 0;
  const bool console =
      GetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), &console_mode) != 0;
  std::printf("rang: stdout tty=%d console=%d\n", _isatty(_fileno(stdout)) != 0,
              console);
#else
  std::printf("rang: stdout tty=%d\n", isatty(fileno(stdout)) != 0);
#endif

  if (failures != 0) {
    std::printf("%d failures\n", failures);
    return 1;
  }
  std::printf("rang: ok\n");
  return 0;
}
