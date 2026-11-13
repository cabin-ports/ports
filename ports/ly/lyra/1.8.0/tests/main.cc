// Consumer test for cabin-ports/lyra: parses fixed command lines and checks
// every bound value, the help text and the failure messages.
#include <lyra/lyra.hpp>

#include <iostream>
#include <sstream>
#include <string>

static_assert(LYRA_VERSION_MAJOR == 1 && LYRA_VERSION_MINOR == 8 && LYRA_VERSION_PATCH == 0, "lyra 1.8.0");

namespace {

int failures = 0;

void check(bool pass, const char *what) {
  if (!pass) {
    std::cerr << "FAIL: " << what << '\n';
    ++failures;
  }
}

struct options {
  int width = 0;
  bool verbose = false;
  std::string name;
  bool help = false;
  bool ok = false;
  std::string help_text;
};

// An int option with a short and a long name, a flag, a positional
// argument and the help flag.
options parse(std::initializer_list<std::string> argv) {
  options o;
  auto cli = lyra::cli() | lyra::help(o.help) | lyra::opt(o.width, "width")["-w"]["--width"]("Column width") |
             lyra::opt(o.verbose)["-v"]["--verbose"]("Print more") | lyra::arg(o.name, "name")("Who to greet");
  o.ok = static_cast<bool>(cli.parse(lyra::args(argv)));
  if (o.help) {
    std::ostringstream text;
    text << cli;
    o.help_text = text.str();
  }
  return o;
}

struct outcome {
  bool ok;
  std::string message;
};

// Without a positional argument, which would take any leftover token.
outcome parse_options_only(std::initializer_list<std::string> argv) {
  int width = 0;
  bool verbose = false;
  auto cli = lyra::cli() | lyra::opt(width, "width")["-w"]["--width"]("Column width") |
             lyra::opt(verbose)["-v"]["--verbose"]("Print more");
  // Only the status and message leave: the result holds iterators into args.
  auto result = cli.parse(lyra::args(argv));
  return {static_cast<bool>(result), result.message()};
}

} // namespace

int main() {
  options o = parse({"prog", "-w", "42", "--verbose", "alice"});
  check(o.ok && o.width == 42 && o.verbose && o.name == "alice" && !o.help, "short option, long flag, positional");

  o = parse({"prog", "--width", "7", "bob"});
  check(o.ok && o.width == 7 && !o.verbose && o.name == "bob", "long option with a separate value");

  o = parse({"prog", "--width=9", "-v", "carol"});
  check(o.ok && o.width == 9 && o.verbose && o.name == "carol", "long option with an = value, short flag");

  o = parse({"prog", "--help"});
  check(o.ok && o.help && o.width == 0 && o.name.empty(), "--help sets the help flag");
  check(o.help_text == "USAGE:\n"
                       "  prog [-?|-h|--help] [-w|--width <width>] [-v|--verbose] [<name>]\n"
                       "\n"
                       "Display usage information.\n"
                       "\n"
                       "OPTIONS, ARGUMENTS:\n"
                       "  -?, -h, --help          \n"
                       "  -w, --width <width>     Column width [default: 0]\n"
                       "  -v, --verbose           Print more\n"
                       "  <name>                  Who to greet\n",
        "the help text lists every parser");

  outcome r = parse_options_only({"prog", "--bogus"});
  check(!r.ok && r.message == "Unrecognized token: --bogus", "an unknown option fails with its name");

  r = parse_options_only({"prog", "-w"});
  check(!r.ok && r.message == "Expected argument following -w", "a missing value fails with the option name");

  r = parse_options_only({"prog", "-w", "forty"});
  check(!r.ok && r.message == "Unable to convert 'forty' to destination type", "a non-integer value fails to convert");

  if (failures != 0) {
    std::cerr << failures << " check(s) failed\n";
    return 1;
  }
  std::cout << "lyra 1.8.0 C++ ok\n";
  return 0;
}
