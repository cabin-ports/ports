#include <argparse/argparse.hpp>

#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

using Strings = std::vector<std::string>;

// tool [-j N] [--verbose] [-O] [--ratio R] [--define K V] inputs...
void Configure(argparse::ArgumentParser& p) {
  p.add_argument("inputs").nargs(argparse::nargs_pattern::at_least_one);
  p.add_argument("-j", "--jobs").default_value(4).scan<'i', int>();
  p.add_argument("--verbose").flag();
  p.add_argument("-O", "--optimize").default_value(0).implicit_value(3).nargs(0);
  p.add_argument("--ratio").default_value(0.5).scan<'g', double>();
  p.add_argument("--define").nargs(2);
}

// Returns the message parse_args throws, or "" when it does not throw.
std::string ParseError(argparse::ArgumentParser& p, const Strings& argv) {
  try {
    p.parse_args(argv);
  } catch (const std::runtime_error& e) {
    return e.what();
  }
  return "";
}

}  // namespace

int main() {
  // Every kind of argument given; --ratio keeps its default.
  {
    argparse::ArgumentParser p("tool", "1.0", argparse::default_arguments::none);
    Configure(p);
    p.parse_args({"tool", "-j", "8", "--verbose", "-O", "--define", "NAME",
                  "VALUE", "a.c", "b.c"});
    check(p.get<int>("--jobs") == 8 && p.get<int>("-j") == 8, "jobs given");
    check(p.get<bool>("--verbose"), "flag given");
    check(p.get<int>("--optimize") == 3, "implicit value");
    check(p.get<Strings>("--define") == Strings{"NAME", "VALUE"}, "nargs(2)");
    check(p.get<Strings>("inputs") == Strings{"a.c", "b.c"}, "positionals");
    check(p.get<double>("--ratio") == 0.5, "ratio default");
    check(p.is_used("--jobs") && !p.is_used("--ratio"), "is_used");
  }

  // The positional and --ratio: the other optionals keep their defaults.
  {
    argparse::ArgumentParser p("tool", "1.0", argparse::default_arguments::none);
    Configure(p);
    p.parse_args({"tool", "--ratio", "0.25", "only.c"});
    check(p.get<int>("--jobs") == 4, "jobs default");
    check(!p.get<bool>("--verbose"), "flag default");
    check(p.get<int>("--optimize") == 0, "optimize default");
    check(p.get<double>("--ratio") == 0.25, "ratio given");
    check(!p.present<Strings>("--define").has_value(), "define absent");
    check(p.get<Strings>("inputs") == Strings{"only.c"}, "one positional");
  }

  // A subcommand with its own arguments.
  {
    argparse::ArgumentParser p("pkg", "1.0", argparse::default_arguments::none);
    argparse::ArgumentParser add("add", "1.0", argparse::default_arguments::none);
    add.add_argument("name");
    add.add_argument("--dev").flag();
    argparse::ArgumentParser remove("remove", "1.0",
                                    argparse::default_arguments::none);
    remove.add_argument("name");
    p.add_subparser(add);
    p.add_subparser(remove);
    p.parse_args({"pkg", "add", "fmt", "--dev"});
    check(p.is_subcommand_used("add") && p.is_subcommand_used(add),
          "subcommand used");
    check(!p.is_subcommand_used("remove"), "other subcommand unused");
    check(add.get("name") == "fmt", "subcommand positional");
    check(add.get<bool>("--dev"), "subcommand flag");
  }

  // Errors.
  {
    argparse::ArgumentParser p("tool", "1.0", argparse::default_arguments::none);
    // Names sort shortest first, so the message uses -o.
    p.add_argument("--output", "-o").required();
    p.add_argument("input");
    check(ParseError(p, {"tool", "in.txt"}) == "-o: required.",
          "missing required optional");
  }
  {
    argparse::ArgumentParser p("tool", "1.0", argparse::default_arguments::none);
    Configure(p);
    check(ParseError(p, {"tool"}) ==
              "inputs: 1 or more argument(s) expected. 0 provided.",
          "missing positionals");
  }
  {
    argparse::ArgumentParser p("tool", "1.0", argparse::default_arguments::none);
    Configure(p);
    check(ParseError(p, {"tool", "--define", "ONLY", "--verbose", "x.c"}) ==
              "Too few arguments for '--define'.",
          "nargs(2) given one value");
  }

  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("argparse consumer test passed");
  return 0;
}
