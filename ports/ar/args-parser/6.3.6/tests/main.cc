// Parses fixed command lines with both of args-parser's APIs and checks the
// values, the positional arguments, the error descriptions, and the printed
// help.  The misspelling and help texts are the ones upstream's own tests
// expect (tests/auto/help/main.cpp).
#include <args-parser/all.hpp>

#include <cstdio>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char *what)
{
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

using Strings = std::vector<std::string>;

// Returns the BaseException description parse() throws, or "(parsed)".
template<int N>
std::string parse(Args::CmdLine &cmd, const char *const (&argv)[N])
{
    try {
        cmd.parse(N, argv);
    } catch (const Args::BaseException &e) {
        return e.desc();
    }
    return "(parsed)";
}

void test_classic_api()
{
    {
        const char *const argv[] = {"prog", "--host=localhost", "-p", "8080", "--timeout", "250"};
        Args::CmdLine cmd;
        Args::Arg host('s', "host", true, true);
        Args::Arg port('p', "port", true, true);
        Args::Arg timeout("timeout", true);
        cmd.addArg(host);
        cmd.addArg(port);
        cmd.addArg(timeout);
        check(parse(cmd, argv) == "(parsed)", "classic: parses");
        check(host.value() == "localhost", "classic: --host=value");
        check(port.value() == "8080", "classic: -p value");
        check(cmd.value("--timeout") == "250", "classic: name-only value");
        check(cmd.isDefined("-s") && cmd.isDefined("--port"), "classic: flag and name both resolve");
    }
    {
        const char *const argv[] = {"prog", "-s", "h"};
        Args::CmdLine cmd;
        Args::Arg host('s', "host", true, true);
        Args::Arg port('p', "port", true, true);
        cmd.addArg(host);
        cmd.addArg(port);
        check(parse(cmd, argv) == "Undefined required argument \"--port\".", "classic: required argument");
    }
    {
        const char *const argv[] = {"prog", "--host"};
        Args::CmdLine cmd;
        Args::Arg host('s', "host", true);
        cmd.addArg(host);
        check(parse(cmd, argv) == "Argument \"--host\" requires value that wasn't presented.",
              "classic: missing value");
    }
}

void test_one_line_api()
{
    {
        const char *const argv[] = {"prog", "-abc=out.txt", "-VVV", "-m", "1", "--multi", "2", "-m", "3"};
        Args::CmdLine cmd;
        Args::MultiArg verbose('V');
        cmd.addArgWithFlagOnly('a')
            .addArgWithFlagOnly('b')
            .addArgWithFlagOnly('c', true)
            .addMultiArg('m', "multi", true)
            .addArgWithNameOnly("level", true, false, "Level.", "", "3")
            .addArg(verbose);
        check(parse(cmd, argv) == "(parsed)", "one-line: parses");
        check(cmd.isDefined("-a") && cmd.isDefined("-b"), "one-line: flag block");
        check(cmd.value("-c") == "out.txt", "one-line: last flag of a block takes the value");
        check(verbose.count() == 3, "one-line: repeated flag counts");
        check(cmd.values("--multi") == Strings{"1", "2", "3"}, "one-line: multi-arg collects values");
        check(!cmd.isDefined("--level") && cmd.value("--level") == "3", "one-line: default value");
    }
    {
        const char *const argv[] = {"prog", "-ca", "x"};
        Args::CmdLine cmd;
        cmd.addArgWithFlagOnly('a').addArgWithFlagOnly('c', true);
        check(parse(cmd, argv) == "Only last argument in flags combo can be with value. Flags combo is \"-ca\".",
              "one-line: value flag inside a block");
    }
    {
        const char *const argv[] = {"prog", "-a", "-a"};
        Args::CmdLine cmd;
        cmd.addArgWithFlagOnly('a');
        check(parse(cmd, argv) == "Argument \"-a\" already defined.", "one-line: repeated single arg");
    }
    {
        const char *const argv[] = {"prog", "-a", "-b"};
        Args::CmdLine cmd;
        cmd.addOnlyOneGroup("mode").addArgWithFlagOnly('a').addArgWithFlagOnly('b').end();
        check(parse(cmd, argv)
                  == "Only one argument can be defined in OnlyOne group \"mode\". Whereas defined \"-a\" and \"-b\".",
              "one-line: OnlyOne group");
    }
}

void test_commands()
{
    {
        const char *const argv[] = {"prog", "add", "-f", "test.txt", "-d", "-r"};
        Args::CmdLine cmd(Args::CmdLine::CommandIsRequired);
        cmd.addCommand("add", Args::ValueOptions::NoValue, false, "Add file.")
            .addArgWithFlagAndName('f', "file", true, false, "Name of the file.", "", "", "fn")
            .addArgWithFlagOnly('d', false, false, "Do job.")
            .end()
            .addCommand("delete", Args::ValueOptions::NoValue, false, "Delete file.")
            .addArgWithFlagOnly('d', false, false, "Do not do the job.")
            .end()
            .addArgWithFlagAndName('r', "recursive", false, false, "Recurse.");
        check(parse(cmd, argv) == "(parsed)", "commands: parses");
        check(cmd.isDefined("add") && !cmd.isDefined("delete"), "commands: chosen command");
        check(cmd.value("--file") == "test.txt", "commands: child argument value");
        check(cmd.isDefined("-d") && cmd.isDefined("-r"), "commands: child and global flags");
    }
    {
        const char *const argv[] = {"prog", "add", "100", "4545", "500", "-f", "local"};
        Args::CmdLine cmd;
        Args::Arg f('f', true, true);
        Args::Command add("add", Args::ValueOptions::ManyValues);
        add.addArg(f);
        cmd.addArg(add);
        check(parse(cmd, argv) == "(parsed)", "commands: many values parses");
        check(add.values() == Strings{"100", "4545", "500"}, "commands: command values");
        check(f.value() == "local", "commands: argument after the values");
    }
    {
        const char *const argv[] = {"prog", "add", "delete"};
        Args::CmdLine cmd;
        cmd.addCommand("add").end().addCommand("delete").end();
        check(parse(cmd, argv)
                  == "Only one command from one level can be specified. But you defined \"delete\" and \"add\".",
              "commands: two commands on one level");
    }
    {
        const char *const argv[] = {"prog"};
        Args::CmdLine cmd(Args::CmdLine::CommandIsRequired);
        cmd.addCommand("add").end();
        check(parse(cmd, argv) == "Not specified command.", "commands: required command");
    }
}

void test_positional()
{
    {
        const char *const argv[] = {"prog", "-v", "in1", "-x", "in2"};
        Args::CmdLine cmd(Args::CmdLine::HandlePositionalArguments);
        cmd.addArgWithFlagOnly('v');
        check(parse(cmd, argv) == "(parsed)", "positional: parses");
        check(cmd.isDefined("-v"), "positional: flag before the positionals");
        check(cmd.positional() == Strings{"in1", "-x", "in2"}, "positional: the rest of the line");
    }
    {
        // A flag-only argument would match "--" itself (see cabin.toml).
        const char *const argv[] = {"prog", "--", "-v"};
        Args::CmdLine cmd(Args::CmdLine::HandlePositionalArguments);
        cmd.addArgWithFlagAndName('v', "verbose");
        check(parse(cmd, argv) == "(parsed)", "positional: -- parses");
        check(!cmd.isDefined("-v") && cmd.positional() == Strings{"-v"}, "positional: -- ends the options");
    }
}

// Upstream's TestSimpleHelp, TestArgHelp and TestMisspelling share this setup.
struct HostCli {
    Args::CmdLine cmd;
    Args::Arg host{'s', "host", true, true};
    Args::Arg port{'p', "port", true, true};
    Args::Arg timeout{"timeout", true};
    Args::Help help;

    HostCli()
    {
        host.setDescription("Host. Can be \"localhost\", \"any\" or regular IP.");
        host.setLongDescription("Host. This argument told to the application where to open socket for communication.");
        port.setDescription("Port number to create socket.");
        timeout.setValueSpecifier("ms");
        timeout.setDescription("Timeout before new messages will be sent in milliseconds.");
        help.setExecutable("executable");
        help.setAppDescription("This application just show the power of Args.");
        cmd.addArg(host);
        cmd.addArg(port);
        cmd.addArg(timeout);
        cmd.addArg(help);
    }
};

// Help prints to Args::outStream(), which is std::cout.  Returns what parsing
// argv printed, or "(no help)" when it did not throw
// HelpHasBeenPrintedException.
template<int N>
std::string help_output(const char *const (&argv)[N])
{
    HostCli cli;
    std::ostringstream out;
    std::streambuf *const saved = std::cout.rdbuf(out.rdbuf());
    bool printed = false;
    try {
        cli.cmd.parse(N, argv);
    } catch (const Args::HelpHasBeenPrintedException &) {
        printed = true;
    } catch (...) {
        std::cout.rdbuf(saved);
        throw;
    }
    std::cout.rdbuf(saved);
    return printed ? out.str() : "(no help)";
}

void test_errors_and_help()
{
    {
        const char *const argv[] = {"prog", "--hots"};
        HostCli cli;
        check(parse(cli.cmd, argv) == "Unknown argument \"--hots\".\n\nProbably you mean \"--host\".",
              "errors: misspelled argument");
    }
    {
        const char *const argv[] = {"prog", "-z"};
        HostCli cli;
        check(parse(cli.cmd, argv) == "Unknown argument \"-z\".", "errors: unknown flag");
    }
    {
        HostCli cli;
        bool threw = false;
        try {
            cli.cmd.addArg(cli.host);
        } catch (const Args::BaseException &e) {
            threw = e.desc() == "Argument \"--host\" already in the command line parser.";
        }
        check(threw, "errors: argument added twice");
    }
    {
        const char *const argv[] = {"prog", "-h"};
        check(help_output(argv)
                  == "This application just show the power of Args. \n"
                     "\n"
                     "USAGE: executable -s, --host <arg> -p, --port <arg> [ -h, --help <arg> ] \n"
                     "       [ --timeout <ms> ] \n"
                     "\n"
                     "REQUIRED:\n"
                     " -s, --host <arg>   Host. Can be \"localhost\", \"any\" or regular IP. \n"
                     "\n"
                     " -p, --port <arg>   Port number to create socket. \n"
                     "\n"
                     "OPTIONAL:\n"
                     " -h, --help <arg>   Print this help. \n"
                     "\n"
                     "     --timeout <ms> Timeout before new messages will be sent in milliseconds. \n\n",
              "help: full help");
    }
    {
        const char *const argv[] = {"prog", "-h=-s"};
        check(help_output(argv)
                  == "USAGE: -s, --host <arg> \n"
                     "\n"
                     "       Host. This argument told to the application where to open socket \n"
                     "       for communication. \n\n",
              "help: help for one argument");
    }
}

} // namespace

int main()
{
    test_classic_api();
    test_one_line_api();
    test_commands();
    test_positional();
    test_errors_and_help();
    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::puts("args-parser 6.3.6 ok");
    return 0;
}
