#include <argagg/argagg.hpp>
#include <argagg/convert/csv.hpp>

#include <cstdio>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char *what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    failures++;
  }
}

template <typename E, typename F>
bool throws(F f) {
  try {
    f();
  } catch (const E &) {
    return true;
  }
  return false;
}

} // namespace

int main() {
  argagg::parser p{{{"help", {"-h", "--help"}, "print help", 0},
                    {"num", {"-n", "--num"}, "a number", 1},
                    {"delim", {"-d"}, "a delimiter", 1},
                    {"list", {"-l", "--list"}, "a list", 1}}};

  const char *argv[] = {"prog", "-n",  "42", "--num=7", "-d",   ";",
                        "pos1", "pos2", "-l", "1,2,3"};
  argagg::parser_results r = p.parse(10, argv);
  check(std::string(r.program) == "prog", "program name");
  check(r["num"].count() == 2, "num count");
  check(r["num"][0].as<int>() == 42, "first num");
  check(r["num"].as<int>() == 7, "last num wins");
  check(r["delim"].as<std::string>() == ";", "delim");
  check(!r["help"] && !r.has_option("help"), "help absent");
  check(r.has_option("num"), "num present");
  check(r["num"].as<int>(-1) == 7, "as with default");
  check(r.count() == 2 && std::string(r.pos[0]) == "pos1", "positional count");
// C2666 on cl, see cabin.toml.
#if !defined(_MSC_VER)
  check(std::string(r[std::size_t(0)]) == "pos1", "positional index");
#endif
  const argagg::parser_results &cr = r;
  check(std::string(cr[std::size_t(1)]) == "pos2", "const positional index");
  check(r.as<std::string>(1) == "pos2", "positional as");
  std::vector<std::string> all = r.all_as<std::string>();
  check(all.size() == 2 && all[1] == "pos2", "all_as");
  check(r["list"].as<argagg::csv<int>>().values == std::vector<int>({1, 2, 3}),
        "csv<int>");

  const char *bad[] = {"prog", "-n", "abc"};
  argagg::parser_results rb = p.parse(3, bad);
  check(throws<std::invalid_argument>([&] { rb["num"].as<int>(); }),
        "non-numeric int throws");
  check(rb["num"].as<int>(5) == 5, "default on failed conversion");
  const char *range[] = {"prog", "-n", "99999999999999999999"};
  argagg::parser_results rr = p.parse(3, range);
  check(throws<std::out_of_range>([&] { rr["num"].as<int>(); }),
        "overflowing int throws");

  const char *unknown[] = {"prog", "-x"};
  check(throws<argagg::unexpected_option_error>([&] { p.parse(2, unknown); }),
        "unknown option");
  const char *noarg[] = {"prog", "-n"};
  check(throws<argagg::option_lacks_argument_error>(
            [&] { p.parse(2, noarg); }),
        "missing argument");
  const char *extra[] = {"prog", "--help=1"};
  check(throws<argagg::unexpected_argument_error>(
            [&] { p.parse(2, extra); }),
        "argument to a flag option");
  check(throws<argagg::unknown_option>([&] { r["nope"]; }), "unknown name");

  std::ostringstream os;
  os << p;
  std::string help = os.str();
  check(help.find("-n, --num") != std::string::npos &&
            help.find("a number") != std::string::npos,
        "help text");

  return failures == 0 ? 0 : 1;
}
