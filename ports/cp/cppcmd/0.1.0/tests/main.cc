#include <cppcmd/CommandInterpreter.hpp>

#include <algorithm>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

static int failures = 0;

static void check(bool ok, const char* what) {
  if (!ok) {
    std::printf("FAIL %s\n", what);
    ++failures;
  }
}

using Args = std::vector<cppcmd::values::ValueString>;

int main() {
  std::istringstream in("add 2 0x10\nflag true\nnope\n");
  std::ostringstream out;
  cppcmd::CommandInterpreter<> cli(in, out);
  int sum = 0;
  bool flag = false;
  cli.registerCommand(
      "add",
      [&](const Args& args, std::ostream& os) {
        sum = args[0].as<int>() + args[1].as<int>();
        os << sum << '\n';
      },
      "adds");
  cli.registerCommand("flag", [&](const Args& args, std::ostream&) {
    flag = args[0].as<bool>();
  });
  cli.run();
  check(sum == 18, "add 2 0x10");
  check(flag, "flag true");

  // An unknown command prints the help list, whose order is unordered_map's.
  const std::string prefix = "18\nunknown command: nope\nAvailable functions:\n";
  const std::string text = out.str();
  check(text.compare(0, prefix.size(), prefix) == 0, "output prefix");
  std::vector<std::string> lines;
  std::istringstream rest(text.substr(std::min(prefix.size(), text.size())));
  for (std::string line; std::getline(rest, line);) {
    lines.push_back(line);
  }
  std::sort(lines.begin(), lines.end());
  const std::vector<std::string> help = {"add       adds", "exit      ",
                                         "flag      ",
                                         "help      this help message"};
  check(lines == help, "help lines");

  check(cppcmd::values::ValueString(std::string("2.5")).as<double>() == 2.5,
        "as<double>");
  bool threw = false;
  try {
    cppcmd::values::ValueString(std::string("zz")).as<int>();
  } catch (const cppcmd::parse_error& e) {
    threw = std::string(e.what()) == "failed to parse";
  }
  check(threw, "as<int> of zz throws parse_error");

  if (failures != 0) {
    return 1;
  }
  std::printf("cppcmd 0.1.0 C++ ok\n");
  return 0;
}
