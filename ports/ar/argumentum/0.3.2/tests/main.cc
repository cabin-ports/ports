#include <argumentum/argparse.h>

#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

static int failures = 0;

#define CHECK(cond)                                                        \
  do {                                                                     \
    if (!(cond)) {                                                         \
      std::fprintf(stderr, "FAILED: %s (line %d)\n", #cond, __LINE__);     \
      ++failures;                                                          \
    }                                                                      \
  } while (0)

int main() {
  using namespace argumentum;
  using Args = std::vector<std::string>;

  {
    std::vector<int> numbers;
    bool isSum = false;
    std::string name;
    auto parser = argument_parser{};
    auto params = parser.params();
    params.add_parameter(numbers, "N").minargs(1).metavar("INT");
    params.add_parameter(isSum, "--sum", "-s").nargs(0);
    params.add_parameter(name, "--name").nargs(1);

    auto res = parser.parse_args(Args{"--sum", "1", "3", "2", "--name", "cabin"});
    CHECK(bool(res));
    CHECK((numbers == std::vector<int>{1, 3, 2}));
    CHECK(isSum);
    CHECK(name == "cabin");
  }

  {
    std::vector<int> numbers;
    std::ostringstream out;
    auto parser = argument_parser{};
    parser.config().cout(out);
    parser.params().add_parameter(numbers, "N").minargs(1);

    auto bad = parser.parse_args(Args{"x"});
    CHECK(!bool(bad));
    CHECK(bad.errors.size() == 1 &&
          bad.errors[0].errorCode == CONVERSION_ERROR);
  }

  {
    std::string mode;
    bool verbose = false;
    std::ostringstream out;
    auto parser = argument_parser{};
    parser.config().program("prog").cout(out);
    auto params = parser.params();
    params.add_parameter(mode, "--mode").nargs(1).required().help("Run mode");
    params.add_parameter(verbose, "--verbose").nargs(0);

    auto info = parser.describe_argument("--mode");
    CHECK(info.help == "Run mode");
    CHECK(info.isRequired);

    auto missing = parser.parse_args(Args{"--verbose"});
    CHECK(!bool(missing));
    CHECK(missing.errors.size() == 1 &&
          missing.errors[0].errorCode == MISSING_OPTION);

    // Without arguments a parser with required parameters prints its help.
    out.str("");
    auto help = parser.parse_args(Args{});
    CHECK(!bool(help));
    CHECK(help.errors.size() == 1 && help.errors[0].errorCode == EXIT_REQUESTED);
    CHECK(out.str().find("--mode") != std::string::npos);
    CHECK(out.str().find("Run mode") != std::string::npos);
  }

  return failures;
}
