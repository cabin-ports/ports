// A scripted file session over a menu: command dispatch, typed arguments,
// submenus, and the error paths, from README.md's file session example.
#include <cli/cli.h>
#include <cli/clifilesession.h>

#include <cstdlib>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>

namespace {

int failures = 0;

void Check(bool ok, const char* what) {
  if (!ok) {
    std::cerr << "FAIL: " << what << '\n';
    ++failures;
  }
}

bool Has(const std::string& s, const std::string& part) {
  return s.find(part) != std::string::npos;
}

}  // namespace

int main() {
  int total = 0;
  std::string last;

  auto root = std::make_unique<cli::Menu>("cli");
  root->Insert(
      "add",
      [&total](std::ostream& out, int a, int b) {
        total += a + b;
        out << a + b << "\n";
      },
      "add help", {"a", "b"});
  root->Insert("echo", [&last](std::ostream& out, const std::string& s) {
    last = s;
    out << s << "\n";
  });
  auto sub = std::make_unique<cli::Menu>("sub");
  sub->Insert("hello", [](std::ostream& out) { out << "hello from sub\n"; });
  root->Insert(std::move(sub));

  cli::Cli cli(std::move(root));
  std::stringstream in(
      "add 2 3\n"
      "echo word\n"
      "nope\n"
      "add x 3\n"
      "add 1\n"
      "add 10 20\n"
      "help\n"
      "sub\n"
      "hello\n"
      "exit\n"
      "add 100 100\n");
  std::stringstream out;
  cli::CliFileSession session(cli, in, out);
  session.Start();
  const std::string o = out.str();

  // The prompt is "<menu>> "; handler output and the library's own
  // messages follow it, and the input lines are not echoed.
  const std::string head =
      "cli> 5\n"
      "cli> word\n"
      "cli> wrong command: nope\n"
      "cli> wrong command: add x 3\n"
      "cli> wrong command: add 1\n"
      "cli> 30\n"
      "cli> Commands available:\n";
  Check(o.compare(0, head.size(), head) == 0, "scripted session output");
  Check(total == 35, "add ran for the two valid calls only");
  Check(last == "word", "string argument reaches the handler");
  Check(Has(o, "add help"), "help lists the description");
  Check(Has(o, "echo"), "help lists a command without description");
  Check(Has(o, "\ncli> sub> hello from sub\nsub> "),
        "submenu prompt and command");
  std::string rest;
  std::getline(in, rest);
  Check(rest == "add 100 100", "input after exit stays unread");

  std::cout << o;
  return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
