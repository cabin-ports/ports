// clara: a composed parser binds options, flags, a lambda and positional
// arguments, in separate, joined (=, :) and bundled short forms; help short
// circuits the parse; bad input fails with a message instead of throwing;
// and the parser prints its usage text.
#include <clara.hpp>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, int line, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "main.cc:%d: check failed: %s\n", line, what);
    ++failures;
  }
}

#define CHECK(expr) check(static_cast<bool>(expr), __LINE__, #expr)

struct Config {
  bool show_help = false;
  int width = 0;
  std::string name;
  bool verbose = false;
  bool quiet = false;
  double ratio = 0.0;
  std::vector<std::string> files;
};

clara::Parser make_parser(Config& config) {
  return clara::Help(config.show_help) |
         clara::Opt(config.width, "width")["-w"]["--width"]("output width") |
         clara::Opt(config.name, "name")["-n"]["--name"]("who to greet") |
         clara::Opt(config.verbose)["-v"]("say more") |
         clara::Opt(config.quiet)["-q"]("say less") |
         clara::Opt([&config](double value) { config.ratio = value; },
                    "ratio")["-r"]("a number, through a lambda") |
         clara::Arg(config.files, "file")("input files");
}

template <typename Result>
bool fails_with(const Result& result, const std::string& message) {
  return !result && result.errorMessage() == message;
}

}  // namespace

int main() {
  {
    Config config;
    const clara::Parser cli = make_parser(config);
    const auto result =
        cli.parse(clara::Args{"prog", "-w", "42", "-n", "bob", "-v", "-r",
                              "0.25", "in.txt", "out.txt"});
    CHECK(result);
    CHECK(result.value().type() == clara::ParseResultType::Matched);
    CHECK(config.width == 42);
    CHECK(config.name == "bob");
    CHECK(config.verbose && !config.quiet && !config.show_help);
    CHECK(config.ratio == 0.25);
    CHECK(config.files == (std::vector<std::string>{"in.txt", "out.txt"}));

    // The usage line names the executable from the last parse.
    std::ostringstream usage;
    usage << cli;
    const std::string expected =
        "usage:\n"
        "  prog [<file> ... ] options\n"
        "\n"
        "where options are:\n"
        "  -?, -h, --help         display usage information\n"
        "  -w, --width <width>    output width\n"
        "  -n, --name <name>      who to greet\n"
        "  -v                     say more\n"
        "  -q                     say less\n"
        "  -r <ratio>             a number, through a lambda\n";
    CHECK(usage.str() == expected);
    if (usage.str() != expected) {
      std::fprintf(stderr, "%s", usage.str().c_str());
    }
  }

  {
    // Values joined with '=' or ':', and bundled short flags.
    Config config;
    const auto result = make_parser(config).parse(
        clara::Args{"prog", "--width=7", "-n:alice", "-vq"});
    CHECK(result);
    CHECK(config.width == 7 && config.name == "alice");
    CHECK(config.verbose && config.quiet);
    CHECK(config.files.empty());
  }

  {
    // argc/argv, as main() receives them.
    const char* argv[] = {"prog", "--name", "carol", "a.txt"};
    Config config;
    CHECK(make_parser(config).parse(clara::Args(4, argv)));
    CHECK(config.name == "carol");
    CHECK(config.files == std::vector<std::string>{"a.txt"});
  }

  {
    // Help stops the parse: later options are left alone.
    Config config;
    const auto result =
        make_parser(config).parse(clara::Args{"prog", "-h", "-w", "5"});
    CHECK(result);
    CHECK(result.value().type() == clara::ParseResultType::ShortCircuitAll);
    CHECK(config.show_help);
    CHECK(config.width == 0);
  }

  {
    // Windows also takes '/' as an option prefix; elsewhere it is an argument.
    Config config;
    CHECK(make_parser(config).parse(clara::Args{"prog", "/w", "3"}));
#ifdef _WIN32
    CHECK(config.width == 3 && config.files.empty());
#else
    CHECK(config.width == 0);
    CHECK(config.files == (std::vector<std::string>{"/w", "3"}));
#endif
  }

  {
    Config config;
    const clara::Parser cli = make_parser(config);
    CHECK(fails_with(cli.parse(clara::Args{"prog", "-w", "x"}),
                     "Unable to convert 'x' to destination type"));
    CHECK(fails_with(cli.parse(clara::Args{"prog", "-w"}),
                     "Expected argument following -w"));
    CHECK(fails_with(cli.parse(clara::Args{"prog", "--bogus"}),
                     "Unrecognised token: --bogus"));
  }

  {
    // A bool option that takes a value reads yes/no words.
    bool color = true;
    const auto cli = clara::Parser() |
                     clara::Opt(color, "yes|no")["--color"]("colour output");
    CHECK(cli.parse(clara::Args{"prog", "--color", "off"}));
    CHECK(!color);
    CHECK(cli.parse(clara::Args{"prog", "--color", "YES"}));
    CHECK(color);
    CHECK(
        fails_with(cli.parse(clara::Args{"prog", "--color", "maybe"}),
                   "Expected a boolean value but did not recognise: 'maybe'"));
  }

  if (failures != 0) {
    return 1;
  }
  std::printf("clara ok\n");
  return 0;
}
