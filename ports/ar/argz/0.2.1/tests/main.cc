#include <argz/argz.hpp>

#include <cstdint>
#include <cstdio>
#include <optional>
#include <stdexcept>
#include <string>

static int failures = 0;

static void check(bool ok, const char* what) {
  if (!ok) {
    std::printf("FAIL %s\n", what);
    ++failures;
  }
}

// The std::runtime_error message, if argz::parse throws one.
template <int N>
static std::optional<std::string> parse_error(argz::about& about,
                                              argz::options& opts,
                                              const char* (&argv)[N]) {
  try {
    argz::parse(about, opts, N, argv);
  } catch (const std::runtime_error& e) {
    return std::string(e.what());
  }
  return std::nullopt;
}

int main() {
  std::string input;
  std::int32_t number = 123;
  bool flag = false;
  std::optional<std::int64_t> opt;
  argz::about about{"argz consumer test", "0.2.1"};
  about.print_help_when_no_options = false;
  argz::options opts{
      {{"input", 'i'}, input, "the input file"},
      {{"number"}, number, "an int"},
      {{"flag"}, flag, "a boolean"},
      {{"opt"}, opt, "an optional int"},
  };

  const char* argv[] = {"prog",  "-i",     "in.txt", "--number",
                        "-42",   "--flag", "--opt",  "1234567890"};
  check(!parse_error(about, opts, argv), "full argv parses");
  check(input == "in.txt", "-i in.txt");
  check(number == -42, "--number -42");
  check(flag, "--flag");
  check(opt.has_value() && *opt == 1234567890, "--opt 1234567890");

  const char* alias[] = {"prog", "-z"};
  check(parse_error(about, opts, alias).has_value(),
        "unknown alias throws std::runtime_error");

  const char* positional[] = {"prog", "in.txt"};
  check(parse_error(about, opts, positional) == "Expected '-'",
        "positional without '-' throws \"Expected '-'\"");

  if (failures != 0) {
    return 1;
  }
  std::printf("argz 0.2.1 C++ ok\n");
  return 0;
}
