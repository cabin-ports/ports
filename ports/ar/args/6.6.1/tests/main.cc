// args: argument vectors parsed into flags, an int value, positionals, a
// validated group and commands, with the exceptions for help, a bad integer,
// an unknown flag and a failed group.
#include <args.hxx>

#include <cstdio>
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

using Args = std::vector<std::string>;

// The message of the exception E that parsing argv throws, or "no throw".
template <typename E>
std::string thrown(args::ArgumentParser& parser, const Args& argv) {
  try {
    parser.ParseArgs(argv);
  } catch (const E& e) {
    return e.what();
  }
  return "no throw";
}

}  // namespace

int main() {
  args::ArgumentParser parser("Copies files.");
  args::HelpFlag help(parser, "help", "Show this help", {'h', "help"});
  args::Flag verbose(parser, "verbose", "Print each file", {'v', "verbose"});
  args::ValueFlag<int> jobs(parser, "N", "Parallel jobs", {'j', "jobs"}, 1);
  args::Positional<std::string> dest(parser, "DEST", "Destination");
  args::PositionalList<std::string> sources(parser, "SOURCES", "Files");
  args::Group mode(parser, "Link or move, not both:",
                   args::Group::Validators::AtMostOne);
  args::Flag linking(mode, "link", "Hard-link instead", {"link"});
  args::Flag moving(mode, "move", "Move instead", {"move"});

  parser.ParseArgs(Args{"-v", "--jobs=4", "out", "a.txt", "b.txt", "--move"});
  CHECK(!help && verbose && moving && !linking);
  CHECK(args::get(jobs) == 4);
  CHECK(args::get(dest) == "out");
  CHECK((args::get(sources) == Args{"a.txt", "b.txt"}));

  // Each parse starts from the defaults; short flags combine, and -j takes
  // the next argument as its value.
  parser.ParseArgs(Args{"-vj", "-3", "out"});
  CHECK(verbose && args::get(jobs) == -3 && args::get(dest) == "out" &&
        args::get(sources).empty());
  parser.ParseArgs(Args{});
  CHECK(!verbose && !jobs && args::get(jobs) == 1 && !dest);

  // The generated help starts with the usage line and gives -j's value name.
  parser.Prog("cp");
  const std::string text = parser.Help();
  CHECK(text.find("  cp {OPTIONS} [DEST] [SOURCES...]\n") == 0);
  CHECK(text.find("      -j[N], --jobs=[N]                 Parallel jobs\n") !=
        std::string::npos);
  CHECK(thrown<args::Help>(parser, Args{"out", "--help"}) == "help");
  CHECK(thrown<args::ParseError>(parser, Args{"--jobs", "4x"}) ==
        "Argument 'N' received invalid value type '4x'");
  CHECK(thrown<args::ParseError>(parser, Args{"--jobs=99999999999"}) ==
        "Argument 'N' received invalid value type '99999999999'");
  CHECK(thrown<args::ParseError>(parser, Args{"--force"}) ==
        "Flag could not be matched: force");
  CHECK(thrown<args::ParseError>(parser, Args{"-x"}) ==
        "Flag could not be matched: 'x'");
  CHECK(thrown<args::ValidationError>(parser, Args{"--link", "--move"}) ==
        "Group validation failed somewhere!");

  // Commands: the one named on the command line is matched, and a flag
  // inside it is parsed only after its name.
  args::ArgumentParser git("A git-like tool.");
  args::Command add(git, "add", "Add files");
  args::PositionalList<std::string> paths(add, "PATHS", "Files to add");
  args::Command commit(git, "commit", "Record changes");
  args::ValueFlag<std::string> message(commit, "MSG", "Message", {'m'});
  git.ParseArgs(Args{"commit", "-m", "fix: typo"});
  CHECK(commit && !add && args::get(message) == "fix: typo");
  git.ParseArgs(Args{"add", "x.c", "y.c"});
  CHECK(add && !commit && (args::get(paths) == Args{"x.c", "y.c"}));
  CHECK(thrown<args::ParseError>(git, Args{"-m", "early"}) ==
        "Flag could not be matched: 'm'");

  if (failures != 0) {
    return 1;
  }
  std::printf("args ok\n");
  return 0;
}
