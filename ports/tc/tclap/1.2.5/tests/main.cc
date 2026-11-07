// The C++ consumer: TCLAP in C++98 - labeled values with defaults, switches
// (combined, counted and defaulting to true), repeated and unlabeled
// arguments, a custom delimiter, parsing argc and argv, value constraints,
// exclusive alternatives, the parse errors and ignoring unmatched words,
// --help and --version through a custom CmdLineOutput, the handled-failure
// path, the usage, version, failure, DocBook and zsh outputs, and `--`
// (last, because its state is process-wide).
#include <tclap/CmdLine.h>
#include <tclap/DocBookOutput.h>
#include <tclap/ZshCompletionOutput.h>

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

static bool ok = true;
static bool finished = false;

// TCLAP can end the process with exit(); an exit before main() finishes
// would otherwise pass as success.
static void check_finished() {
    if (!finished) {
        std::fprintf(stderr, "FAIL: the process exited before the tests finished\n");
        std::abort();
    }
}

static void check(bool pass, const char *what) {
    if (!pass) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ok = false;
    }
}

static bool contains(const std::string &text, const std::string &part) {
    return text.find(part) != std::string::npos;
}

// The command line `prog` followed by the space-separated words.
static std::vector<std::string> line(const char *words) {
    std::vector<std::string> args(1, "prog");
    std::istringstream in(words);
    std::string word;
    while (in >> word) args.push_back(word);
    return args;
}

// A command line for a small compiler driver.
struct Driver {
    TCLAP::CmdLine cmd;
    TCLAP::ValueArg<std::string> name;
    TCLAP::ValueArg<int> jobs;
    TCLAP::ValueArg<double> ratio;
    TCLAP::SwitchArg verbose;
    TCLAP::MultiSwitchArg level;
    TCLAP::MultiArg<std::string> includes;
    TCLAP::MultiArg<int> ports;
    TCLAP::UnlabeledMultiArg<std::string> files;

    explicit Driver(char delimiter = ' ')
        : cmd("Compiles the given files.", delimiter, "2.5.1"),
          name("n", "name", "Output name", true, "", "string", cmd),
          jobs("j", "jobs", "Parallel jobs", false, 1, "int", cmd),
          ratio("r", "ratio", "Scale", false, 0.5, "double", cmd),
          verbose("v", "verbose", "Talk more", cmd, false),
          level("O", "optimize", "Raise the optimization level", cmd),
          includes("I", "include", "Include directory", false, "dir", cmd),
          ports("p", "port", "Port number", false, "int", cmd),
          files("files", "Inputs", false, "file", cmd) {
        cmd.setExceptionHandling(false);
    }

    // Parses the words; a parse error propagates.
    void parse_or_throw(const char *words) {
        std::vector<std::string> args = line(words);
        cmd.parse(args);
    }

    // Parses the words, reporting a parse error as a failed check.
    void parse(const char *words) {
        try {
            parse_or_throw(words);
        } catch (TCLAP::ArgException &e) {
            std::fprintf(stderr, "FAIL: parsing \"%s\": %s\n", words, e.error().c_str());
            ok = false;
        }
    }
};

static void parses_values() {
    Driver d;
    d.parse("-n app --jobs 8 -r 2.5 -vOO -O -I a --include b -p 80 -p 443 x.cc y.cc");
    check(d.name.getValue() == "app" && d.name.isSet(), "a required value");
    check(d.jobs.getValue() == 8 && d.jobs.isSet(), "a value by its long name");
    check(d.ratio.getValue() == 2.5, "a floating-point value");
    check(d.verbose.getValue() && d.verbose.isSet(), "a switch inside combined switches");
    check(d.level.getValue() == 3, "a counted switch, combined and alone");
    check(d.includes.getValue().size() == 2 && d.includes.getValue()[0] == "a" && d.includes.getValue()[1] == "b",
          "a repeated value in order, by short and long name");
    check(d.ports.getValue().size() == 2 && d.ports.getValue()[0] == 80 && d.ports.getValue()[1] == 443,
          "a repeated integer value");
    check(d.files.getValue().size() == 2 && d.files.getValue()[0] == "x.cc" && d.files.getValue()[1] == "y.cc",
          "unlabeled values collect the rest");
    check(d.cmd.getProgramName() == "prog", "the program name");

    Driver defaults;
    defaults.parse("--name app");
    check(defaults.jobs.getValue() == 1 && !defaults.jobs.isSet() && defaults.ratio.getValue() == 0.5 &&
              !defaults.verbose.getValue() && defaults.level.getValue() == 0 && defaults.includes.getValue().empty() &&
              defaults.files.getValue().empty(),
          "unset arguments keep their defaults");

    Driver equals('=');
    equals.parse("--name=app -j=3");
    check(equals.name.getValue() == "app" && equals.jobs.getValue() == 3, "a custom name-value delimiter");
}

// The error that parsing the words raises, or "" if it succeeds.
static std::string parse_error(const char *words, std::string *arg_id = 0) {
    Driver d;
    try {
        d.parse_or_throw(words);
    } catch (TCLAP::ArgException &e) {
        if (arg_id) *arg_id = e.argId();
        return e.error();
    }
    return "";
}

static void rejects_bad_lines() {
    check(contains(parse_error(""), "Required argument missing: name"), "a missing required argument");
    TCLAP::CmdLine strict("Takes no files.", ' ', "1.0");
    strict.setExceptionHandling(false);
    TCLAP::SwitchArg quiet("q", "quiet", "Say less", strict, false);
    std::vector<std::string> args = line("-q --bogus");
    std::string error, what;
    try {
        strict.parse(args);
    } catch (TCLAP::ArgException &e) {
        error = e.error();
        what = e.what();
    }
    check(quiet.getValue() && contains(error, "Couldn't find match for argument") &&
              what == "--bogus -- Couldn't find match for argument",
          "an unknown argument");

    TCLAP::CmdLine lenient("Ignores what it does not know.", ' ', "1.0");
    lenient.setExceptionHandling(false);
    lenient.ignoreUnmatched(true);
    TCLAP::SwitchArg strip("S", "no-strip", "Keep the symbols", lenient, true);
    args = line("--bogus -S");
    error.clear();
    try {
        lenient.parse(args);
    } catch (TCLAP::ArgException &e) {
        error = e.error();
    }
    check(error.empty() && !strip.getValue(), "an ignored unknown argument and a switch that defaults to true");
    std::string id;
    check(contains(parse_error("-n app -j many", &id), "many") && contains(id, "--jobs"), "a non-numeric value");
    check(contains(parse_error("-n app -j"), "Missing a value"), "a value missing at the end");
    check(contains(parse_error("-n app -n again"), "already set"), "a value given twice");
    check(parse_error("-n app -j 4").empty(), "a good line raises nothing");
}

// One positional argument and a value with a space, from argc and argv as
// main() receives them.
static void parses_argv() {
    TCLAP::CmdLine cmd("Copies one file.", ' ', "1.0");
    cmd.setExceptionHandling(false);
    TCLAP::ValueArg<std::string> title("t", "title", "Title", false, "", "string", cmd);
    TCLAP::UnlabeledValueArg<std::string> source("source", "The input", true, "", "file", cmd);
    const char *const argv[] = {"copy", "-t", "two words", "my file.txt"};
    try {
        cmd.parse(4, argv);
    } catch (TCLAP::ArgException &e) {
        std::fprintf(stderr, "FAIL: parsing argv: %s\n", e.error().c_str());
        ok = false;
    }
    check(cmd.getProgramName() == "copy" && title.getValue() == "two words" && source.getValue() == "my file.txt",
          "argv with a positional argument and values with spaces");

    const char *const missing[] = {"copy", "-t", "x"};
    std::string error;
    try {
        cmd.reset();
        cmd.parse(3, missing);
    } catch (TCLAP::ArgException &e) {
        error = e.error();
    }
    check(contains(error, "Required argument missing: source"), "a missing positional argument");
}

static void constrains_values() {
    std::vector<std::string> modes;
    modes.push_back("fast");
    modes.push_back("small");
    TCLAP::ValuesConstraint<std::string> allowed(modes);
    TCLAP::CmdLine cmd("Picks a mode.", ' ', "1.0");
    cmd.setExceptionHandling(false);
    TCLAP::ValueArg<std::string> mode("m", "mode", "Mode", false, "fast", &allowed, cmd);
    TCLAP::ValueArg<int> width("w", "width", "Width", false, 0, "int");
    TCLAP::ValueArg<int> height("t", "height", "Height", false, 0, "int");
    cmd.xorAdd(width, height);
    check(allowed.description() == "fast|small", "a constraint describes its values");

    std::vector<std::string> args = line("-m small -w 3");
    std::string error;
    try {
        cmd.parse(args);
    } catch (TCLAP::ArgException &e) {
        error = e.error();
    }
    check(error.empty() && mode.getValue() == "small" && width.getValue() == 3 && !height.isSet(),
          "an allowed value and one alternative");

    try {
        cmd.reset();
        args = line("-m tiny -w 3");
        cmd.parse(args);
    } catch (TCLAP::ArgException &e) {
        error = e.error();
    }
    check(contains(error, "tiny") && contains(error, "fast|small"), "a value outside the constraint");

    error.clear();
    try {
        cmd.reset();
        args = line("-w 3 -t 4");
        cmd.parse(args);
    } catch (TCLAP::ArgException &e) {
        error = e.error();
    }
    check(contains(error, "Mutually exclusive"), "exclusive alternatives given together");

    error.clear();
    try {
        cmd.reset();
        args = line("-m small");
        cmd.parse(args);
    } catch (TCLAP::ArgException &e) {
        error = e.error();
    }
    check(contains(error, "missing") && contains(error, "width") && contains(error, "height"),
          "exclusive alternatives given neither");
}

// Records what the command line asks it to print.
class Recorder : public TCLAP::CmdLineOutput {
public:
    Recorder() : usages(0), versions(0) {}
    virtual void usage(TCLAP::CmdLineInterface &c) {
        ++usages;
        message = c.getMessage();
    }
    virtual void version(TCLAP::CmdLineInterface &c) {
        ++versions;
        message = c.getVersion();
    }
    virtual void failure(TCLAP::CmdLineInterface &, TCLAP::ArgException &e) {
        failure_text = e.error();
        throw std::string("failure reported");
    }

    int usages, versions;
    std::string message, failure_text;
};

// The exit status that --help or --version asks for, or -1 if none.
static int exit_status(Driver &d, const char *words) {
    try {
        d.parse_or_throw(words);
    } catch (TCLAP::ExitException &e) {
        return e.getExitStatus();
    } catch (TCLAP::ArgException &) {
    }
    return -1;
}

static void prints_help_and_version() {
    Recorder recorder;
    Driver help;
    help.cmd.setOutput(&recorder);
    check(exit_status(help, "--help") == 0 && recorder.usages == 1 && recorder.versions == 0 &&
              recorder.message == "Compiles the given files.",
          "--help prints the usage and asks to exit with 0");
    Driver version;
    version.cmd.setOutput(&recorder);
    check(exit_status(version, "--version") == 0 && recorder.versions == 1 && recorder.usages == 1 &&
              recorder.message == "2.5.1",
          "--version prints the version and asks to exit with 0");

    // With exception handling on, a failure goes to the output, which here
    // throws instead of exiting.
    Driver handled;
    handled.cmd.setExceptionHandling(true);
    handled.cmd.setOutput(&recorder);
    std::string thrown;
    try {
        handled.parse_or_throw("-j 2");
    } catch (const std::string &s) {
        thrown = s;
    }
    check(thrown == "failure reported" && contains(recorder.failure_text, "Required argument missing: name"),
          "a handled failure is reported to the output");
}

// What the output's usage(), or version(), writes to std::cout.
static std::string printed(TCLAP::CmdLineOutput &output, TCLAP::CmdLineInterface &cmd, bool version = false) {
    std::ostringstream captured;
    std::streambuf *const saved = std::cout.rdbuf(captured.rdbuf());
    if (version)
        output.version(cmd);
    else
        output.usage(cmd);
    std::cout.rdbuf(saved);
    return captured.str();
}

static void prints_outputs() {
    Driver d;
    TCLAP::StdOutput std_output;
    const std::string usage = printed(std_output, d.cmd);
    check(contains(usage, "USAGE:") && contains(usage, "-n <string>") && contains(usage, "--jobs <int>") &&
              contains(usage, "Parallel jobs") && contains(usage, "Compiles the given files.") &&
              contains(usage, "<file> ...") && contains(usage, "--version"),
          "the standard usage lists the arguments");
    check(contains(printed(std_output, d.cmd, true), "version: 2.5.1"), "the standard output prints the version");

    TCLAP::ArgException bad("Bad value", "jobs");
    std::ostringstream errors;
    std::streambuf *const saved = std::cerr.rdbuf(errors.rdbuf());
    int status = -1;
    try {
        std_output.failure(d.cmd, bad);
    } catch (TCLAP::ExitException &e) {
        status = e.getExitStatus();
    }
    std::cerr.rdbuf(saved);
    check(status == 1 && contains(errors.str(), "PARSE ERROR: Argument: jobs") && contains(errors.str(), "Bad value"),
          "a standard failure goes to std::cerr and asks to exit with 1");

    TCLAP::DocBookOutput docbook;
    const std::string doc = printed(docbook, d.cmd);
    check(contains(doc, "<refentry>") && contains(doc, "--jobs") && contains(doc, "Parallel jobs"),
          "the DocBook output describes the arguments");

    TCLAP::ZshCompletionOutput zsh;
    const std::string completion = printed(zsh, d.cmd);
    check(contains(completion, "#compdef") && contains(completion, "_arguments") &&
              contains(completion, "{-j,--jobs}'[parallel jobs]:int'"),
          "the zsh completion lists the arguments");
}

static void ignores_the_rest() {
    Driver d;
    d.parse("-n app -v -- -O --jobs 9");
    check(d.verbose.getValue() && d.level.getValue() == 0 && d.jobs.getValue() == 1 && !d.jobs.isSet(),
          "labeled arguments after -- are not parsed");
    check(d.files.getValue().size() == 3 && d.files.getValue()[0] == "-O" &&
              d.files.getValue()[1] == "--jobs" && d.files.getValue()[2] == "9",
          "the words after -- go to the unlabeled arguments");
}

int main() {
    std::atexit(check_finished);
    parses_values();
    rejects_bad_lines();
    parses_argv();
    constrains_values();
    prints_help_and_version();
    prints_outputs();
    ignores_the_rest();  // last: `--` stays in effect for the process
    finished = true;
    std::printf("tclap (C++): %s\n", ok ? "ok" : "FAILED");
    return ok ? 0 : 1;
}
