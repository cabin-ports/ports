#include <termcolor/termcolor.hpp>

#include <cstdio>
#include <sstream>
#include <string>

namespace {

int failures = 0;

void check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

// String streams are never terminals, so the output below does not depend on
// how the test is run.  On Windows the default implementation is the console
// API, which writes nothing into a stream that is not a console.
#if defined(_WIN32)
const bool kAnsi = false;
#else
const bool kAnsi = true;
#endif

std::string Esc(const std::string& codes) {
  return kAnsi ? "\033[" + codes + "m" : std::string();
}

void TestDefaultIsPlain() {
  std::ostringstream out;
  out << termcolor::red << termcolor::bold << "plain" << termcolor::reset;
  check(out.str() == "plain", "a stream without colorize stays plain");
}

void TestColorize() {
  std::ostringstream out;
  out << termcolor::colorize << termcolor::red << "red" << termcolor::reset;
  check(out.str() == Esc("31") + "red" + Esc("00"), "colorize red");

  std::ostringstream attrs;
  attrs << termcolor::colorize << termcolor::bold << termcolor::underline
        << termcolor::on_blue << termcolor::bright_green
        << termcolor::on_bright_white << "x";
  check(attrs.str() == Esc("1") + Esc("4") + Esc("44") + Esc("92") +
                           Esc("107") + "x",
        "attributes and colors");

  std::ostringstream palette;
  palette << termcolor::colorize << termcolor::color<196>
          << termcolor::on_color<21> << termcolor::color<255, 128, 0>
          << termcolor::on_color<0, 64, 255> << "y";
  check(palette.str() == Esc("38;5;196") + Esc("48;5;21") +
                             Esc("38;2;255;128;0") + Esc("48;2;0;64;255") +
                             "y",
        "256 colors and true colors");
}

void TestNocolorize() {
  std::ostringstream out;
  out << termcolor::colorize << termcolor::cyan << "a"
      << termcolor::nocolorize << termcolor::cyan << "b";
  check(out.str() == Esc("36") + "ab", "nocolorize turns colors off");

  std::ostringstream other;
  other << termcolor::magenta << "c";
  check(other.str() == "c", "colorize is per stream");
}

void TestWide() {
  std::wostringstream out;
  out << termcolor::colorize << termcolor::yellow << L"w" << termcolor::reset;
  const std::wstring expected =
      kAnsi ? std::wstring(L"\033[33mw\033[00m") : std::wstring(L"w");
  check(out.str() == expected, "wide streams");
}

}  // namespace

int main() {
  TestDefaultIsPlain();
  TestColorize();
  TestNocolorize();
  TestWide();
  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("termcolor consumer test passed");
  return 0;
}
