// Parses a fixed command line with store, store_true, int, choice and count
// options, then checks values, positional arguments, an invalid choice
// (error() throws 2 after printing to stderr) and the help text.
#include "OptionParser.h"

#include <cstdio>
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

} // namespace

int main()
{
    std::vector<std::string> modes;
    modes.push_back("fast");
    modes.push_back("slow");

    optparse::OptionParser parser;
    parser.prog("demo").usage("%prog [OPTION]... FILE");
    parser.add_option("-f", "--file").dest("file").help("input file");
    parser.add_option("-q", "--quiet").action("store_true").dest("quiet");
    parser.add_option("-n", "--num").dest("num").type("int").set_default(7);
    parser.add_option("-m", "--mode").dest("mode")
        .choices(modes.begin(), modes.end());
    parser.add_option("-v", "--verbose").action("count").dest("verbose");

    std::vector<std::string> args;
    args.push_back("-f");
    args.push_back("in.txt");
    args.push_back("--quiet");
    args.push_back("-vvv");
    args.push_back("--mode=slow");
    args.push_back("pos1");
    args.push_back("-n");
    args.push_back("42");
    args.push_back("pos2");

    optparse::Values &opts = parser.parse_args(args);
    check(opts["file"] == "in.txt", "store option");
    check(opts.is_set_by_user("file"), "store option is set by user");
    check(static_cast<bool>(opts.get("quiet")), "store_true flag");
    check(opts["quiet"] == "1", "store_true value");
    check(static_cast<int>(opts.get("num")) == 42, "int option");
    check(opts["mode"] == "slow", "choice option");
    check(static_cast<int>(opts.get("verbose")) == 3, "count action");
    check(!opts.is_set("missing"), "unset option");
    check(parser.args().size() == 2 && parser.args()[0] == "pos1" &&
              parser.args()[1] == "pos2",
          "positional arguments");

    // An option left out keeps its default.
    optparse::OptionParser defaults;
    defaults.add_option("-n", "--num").dest("num").type("int").set_default(7);
    defaults.add_option("-q", "--quiet").action("store_true").dest("quiet");
    std::vector<std::string> none;
    optparse::Values &d = defaults.parse_args(none);
    check(static_cast<int>(d.get("num")) == 7, "default value");
    check(!d.is_set_by_user("num"), "default is not set by user");
    check(!static_cast<bool>(d.get("quiet")), "unset flag is false");

    // error() prints to stderr, then exit() throws the int 2.
    std::vector<std::string> bad;
    bad.push_back("--mode=medium");
    optparse::OptionParser strict;
    strict.add_option("-m", "--mode").dest("mode")
        .choices(modes.begin(), modes.end());
    int thrown = 0;
    try {
        strict.parse_args(bad);
    } catch (int code) {
        thrown = code;
    }
    check(thrown == 2, "invalid choice errors");

    std::vector<std::string> badint;
    badint.push_back("--num=abc");
    optparse::OptionParser ints;
    ints.add_option("-n", "--num").dest("num").type("int");
    thrown = 0;
    try {
        ints.parse_args(badint);
    } catch (int code) {
        thrown = code;
    }
    check(thrown == 2, "invalid int errors");

    const std::string help = parser.format_help();
    check(help.find("Usage: demo [OPTION]... FILE") != std::string::npos,
          "help usage line");
    check(help.find("-f FILE, --file=FILE") != std::string::npos,
          "help store option");
    check(help.find("input file") != std::string::npos, "help text");
    check(help.find("-q, --quiet") != std::string::npos, "help flag");
    check(help.find("-m MODE, --mode=MODE") != std::string::npos,
          "help choice option");
    check(help.find("-h, --help") != std::string::npos, "help option");

    return failures == 0 ? 0 : 1;
}
