// Parses fixed argument lists with clipp and checks the bound values, the
// failure reports, and the generated usage and documentation text.  The
// joinable-flag cases and the formatted texts, including the deliberately
// odd separators, are the ones upstream's own tests expect
// (test/joined_flags_test1.cpp, usage_lines_test.cpp, documentation_test.cpp).
#include <clipp.h>

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

void check_text(const std::string &observed, const std::string &expected, const char *what)
{
    if (observed != expected) {
        std::fprintf(stderr, "FAIL: %s\nexpected:\n%s\nobserved:\n%s\n", what, expected.c_str(), observed.c_str());
        ++failures;
    }
}

void test_values()
{
    bool verbose = false;
    int count = 0;
    std::vector<std::string> dirs;
    std::string file;
    auto cli = (clipp::option("-v", "--verbose").set(verbose),
                clipp::option("-n") & clipp::value("count", count),
                clipp::repeatable(clipp::option("-I") & clipp::value("dir", dirs)),
                clipp::value("file", file));

    auto result = clipp::parse({"--verbose", "-n", "42", "-I", "a", "-I", "b", "in.txt"}, cli);
    check(static_cast<bool>(result), "values: parses");
    check(verbose, "values: option sets a bool");
    check(count == 42, "values: value converts to int");
    check(dirs == std::vector<std::string>{"a", "b"}, "values: repeatable group appends");
    check(file == "in.txt", "values: positional value");

    // The argc/argv overload skips argv[0].
    bool quiet = false;
    std::string name;
    auto cli2 = (clipp::option("-q").set(quiet), clipp::value("name", name));
    char a0[] = "prog";
    char a1[] = "-q";
    char a2[] = "x.txt";
    char *argv[] = {a0, a1, a2};
    check(static_cast<bool>(clipp::parse(3, argv, cli2)) && quiet && name == "x.txt", "values: argc/argv");
}

void test_commands_and_failures()
{
    {
        int mode = 0;
        std::string target;
        auto cli = ((clipp::command("make").set(mode, 1) | clipp::command("clean").set(mode, 2)),
                    clipp::opt_value("target", target));
        check(static_cast<bool>(clipp::parse({"clean", "all"}, cli)) && mode == 2 && target == "all",
              "commands: the chosen alternative");
    }
    {
        bool a = false;
        auto result = clipp::parse({"--bogus"}, clipp::group(clipp::option("-a").set(a)));
        check(!result && result.unmapped_args_count() == 1 && !a, "failures: unknown argument");
    }
    {
        std::string in;
        auto result = clipp::parse(clipp::arg_list{}, (clipp::required("-i") & clipp::value("in", in)));
        check(!result && !result.missing().empty(), "failures: missing required option");
    }
}

// Filtered values are the API that instantiates clipp.h's std::result_of
// traits (the enable_if on value(Filter&&, ...)).
void test_filters()
{
    int n = 0;
    check(static_cast<bool>(clipp::parse({"12"}, clipp::group(clipp::value(clipp::match::integers{}, "n", n))))
              && n == 12,
          "filters: integer accepted");
    n = 7; // "x" would convert to 0 if it were assigned
    check(!clipp::parse({"x"}, clipp::group(clipp::value(clipp::match::integers{}, "n", n))) && n == 7,
          "filters: non-integer rejected");

    std::string code;
    auto three = clipp::group(clipp::value([](const std::string &s) { return s.size() == 3; }, "code", code));
    check(static_cast<bool>(clipp::parse({"abc"}, three)) && code == "abc", "filters: predicate accepts");
    code.clear();
    check(!clipp::parse({"abcd"}, three) && code.empty(), "filters: predicate rejects");
}

void test_joinable()
{
    bool a = false;
    bool b = false;
    auto cli = clipp::joinable(clipp::option("a").set(a), clipp::option("b").set(b));
    clipp::parse({"ba"}, cli);
    check(a && b, "joinable: joined flags");
    a = b = false;
    clipp::parse({"-ab"}, cli);
    check(!a && !b, "joinable: unknown prefix matches nothing");
}

void test_formatting()
{
    auto usage_fmt = clipp::doc_formatting{}
                         .first_column(3)
                         .doc_column(9)
                         .last_column(80)
                         .empty_label("?")
                         .param_separator("%")
                         .group_separator("#")
                         .alternative_param_separator("|")
                         .alternative_group_separator("^")
                         .flag_separator("_")
                         .surround_labels("<", ">")
                         .surround_optional("[", "]")
                         .surround_repeat(",,,", "...")
                         .surround_alternatives("§(§", "§)§")
                         .surround_group("(", ")")
                         .surround_joinable("$($", "$)$")
                         .max_flags_per_param_in_doc(2)
                         .max_flags_per_param_in_usage(1)
                         .line_spacing(0)
                         .paragraph_spacing(3)
                         .alternatives_min_split_size(3)
                         .merge_alternative_flags_with_common_prefix(true)
                         .merge_joinable_with_common_prefix(true)
                         .split_alternatives(true);
    check_text(clipp::usage_lines(clipp::group(clipp::option("-a").repeatable(true)), usage_fmt).str(),
               "   ,,,[-a]...", "usage: repeatable option");
    auto counts = ((clipp::option("-n", "--count") & clipp::value("count")) % "number of iterations",
                   (clipp::option("-r", "--ratio") & clipp::value("ratio")) % "compression ratio",
                   (clipp::option("-m") & clipp::opt_value("lines=5")) % "merge lines (default: 5)");
    check_text(clipp::usage_lines(counts, usage_fmt).str(), "   [-n%<count>]#[-r%<ratio>]#[-m%[<lines=5>]]",
               "usage: options with values");

    auto doc_fmt = clipp::doc_formatting{}
                       .first_column(3)
                       .doc_column(45)
                       .last_column(80)
                       .empty_label("?")
                       .param_separator("%")
                       .group_separator("#")
                       .alternative_param_separator("|")
                       .alternative_group_separator("^")
                       .flag_separator("~")
                       .surround_labels("<", ">")
                       .surround_optional("[", "]")
                       .surround_repeat(",,,", "...")
                       .surround_alternatives("§(§", "§)§")
                       .surround_alternative_flags("^(^", "^)^")
                       .surround_group("(", ")")
                       .surround_joinable("$($", "$)$")
                       .max_flags_per_param_in_doc(2)
                       .max_flags_per_param_in_usage(1)
                       .line_spacing(1)
                       .paragraph_spacing(3)
                       .alternatives_min_split_size(3)
                       .merge_alternative_flags_with_common_prefix(false)
                       .merge_joinable_with_common_prefix(true)
                       .split_alternatives(true);
    check_text(clipp::documentation(counts, doc_fmt).str(),
               "   ^(^-n~--count^)^%<count>                  number of iterations\n"
               "\n"
               "   ^(^-r~--ratio^)^%<ratio>                  compression ratio\n"
               "\n"
               "   -m%<lines=5>                              merge lines (default: 5)",
               "documentation: options with values");
    auto align = ((clipp::option("-o", "--out") & clipp::value("output file")) % "output filename",
                  clipp::with_prefix("-f", clipp::option("align") >> [] {} | clipp::option("noalign") >> [] {})
                      % "control alignment");
    check_text(clipp::documentation(align, doc_fmt).str(),
               "   ^(^-o~--out^)^%<output file>              output filename\n"
               "\n"
               "   -falign|-fnoalign                         control alignment",
               "documentation: prefixed alternatives");
}

} // namespace

int main()
{
    test_values();
    test_commands_and_failures();
    test_filters();
    test_joinable();
    test_formatting();
    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::puts("clipp 1.2.3 ok");
    return 0;
}
