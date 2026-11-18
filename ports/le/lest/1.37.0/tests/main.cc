// Runs lest test specifications into string streams and checks the failure counts, summaries and
// reports that lest.hpp and README.md define: a failed assertion ends its case, expressions are
// decomposed, and exceptions are reported as expected or unexpected.
#include <lest/lest.hpp>

#include <cstdio>
#include <new>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char *what, int line) {
    if (!ok) {
        std::fprintf(stderr, "main.cc:%d: check failed: %s\n", line, what);
        ++failures;
    }
}
#define CHECK(x) check((x), #x, __LINE__)

bool contains(const std::string &s, const std::string &part) { return s.find(part) != std::string::npos; }

bool endsWith(const std::string &s, const std::string &tail) {
    return s.size() >= tail.size() && s.compare(s.size() - tail.size(), tail.size(), tail) == 0;
}

void noThrow() {}
void throwRuntime() { throw std::runtime_error("oops"); }

int sections = 0;

// Each assertion macro passes and fails at least once; the cases that fail in a default run say so.
const lest::test mixed[] = {
    CASE("adds numbers") { EXPECT(1 + 1 == 2); },
    CASE("compares strings (fail)") {
        EXPECT(std::string("a") == std::string("b"));
        ++sections;  // Never reached: the failed assertion ends the case.
    },
    CASE("negates and tolerates") {
        EXPECT_NOT(1 == 2);
        EXPECT_NO_THROW(noThrow());
        EXPECT(1.23 == lest::approx(1.23));
        EXPECT(1.23 != lest::approx(1.231));
        EXPECT(1.23 == lest::approx(1.231).epsilon(0.1));
    },
    CASE("negates a true expression (fail)") { EXPECT_NOT(2 == 2); },
    CASE("throws where none is expected (fail)") { EXPECT_NO_THROW(throwRuntime()); },
    CASE("catches the expected exceptions") {
        EXPECT_THROWS(throwRuntime());
        EXPECT_THROWS_AS(throw std::bad_alloc(), std::bad_alloc);
    },
    CASE("misses an exception (fail)") { EXPECT_THROWS(noThrow()); },
    CASE("gets the wrong exception (fail)") { EXPECT_THROWS_AS(throwRuntime(), std::bad_alloc); },
    CASE("throws unexpectedly (fail)") { EXPECT((throw std::runtime_error("surprise!"), true)); },
    CASE("sets up each section afresh") {
        SETUP("a vector of one") {
            std::vector<int> v(1, 7);
            SECTION("grows") {
                v.push_back(8);
                EXPECT(v.size() == 2u);
                ++sections;
            }
            SECTION("starts again from one element") {
                EXPECT(v.size() == 1u);
                ++sections;
            }
        }
    },
    CASE("is hidden [hide]") { EXPECT(false); },
};

const lest::test passing[] = {
    CASE("first") { EXPECT(2 * 3 == 6); },
    CASE("second") { EXPECT(std::string("ab") + "c" == "abc"); },
};

}  // namespace

int main() {
    std::ostringstream os;
    const int failed = lest::run(mixed, os);
    const std::string report = os.str();
    // The [hide] case is not selected, and only the six (fail) cases fail.
    CHECK(failed == 6);
    CHECK(endsWith(report, "6 out of 10 selected tests failed.\n"));
    CHECK(contains(report, ": failed: compares strings (fail): std::string(\"a\") == std::string(\"b\") for \"a\" == \"b\"\n"));
    CHECK(contains(report, ": failed: negates a true expression (fail): ! ( 2 == 2 ) for ! ( 2 == 2 )\n"));
    CHECK(contains(report, ": failed: got unexpected exception with message \"oops\": throws where none is expected (fail): throwRuntime()\n"));
    CHECK(contains(report, ": failed: didn't get exception: misses an exception (fail): noThrow()\n"));
    CHECK(contains(report, ": failed: didn't get exception of type std::bad_alloc: gets the wrong exception (fail): throwRuntime()\n"));
    CHECK(contains(report, ": failed: got unexpected exception with message \"surprise!\": throws unexpectedly (fail)"));
    // Without -p, passing assertions are not reported.
    CHECK(!contains(report, "passed") && !contains(report, "is hidden"));
    CHECK(sections == 2);

    // CASE_ON's extra arguments are the lambda's captures.
    int captured = 0;
    const lest::test capturing[] = {
        CASE_ON("captures a local", &captured) {
            captured = 42;
            EXPECT(captured == 42);
        },
    };
    std::ostringstream capturedOutput;
    CHECK(lest::run(capturing, capturedOutput) == 0 && captured == 42);

    // A passing run prints nothing unless asked to with -p.
    std::ostringstream quiet;
    CHECK(lest::run(passing, quiet) == 0 && quiet.str().empty());
    std::ostringstream loud;
    CHECK(lest::run(passing, lest::texts(1, "-p"), loud) == 0);
    CHECK(contains(loud.str(), "passed: first") && endsWith(loud.str(), "All 2 selected tests passed.\n"));

    // Arguments select cases by text, case-insensitively, and a leading ! omits them.
    std::ostringstream selected;
    CHECK(lest::run(mixed, lest::texts(1, "STRINGS"), selected) == 1);
    CHECK(endsWith(selected.str(), "1 out of 1 selected test failed.\n"));
    std::ostringstream omitted;
    const char *const omitFailing[] = {"-p", "!(fail)"};
    CHECK(lest::run(mixed, lest::texts(omitFailing, omitFailing + 2), omitted) == 0);
    CHECK(endsWith(omitted.str(), "All 4 selected tests passed.\n"));
    std::ostringstream hidden;
    CHECK(lest::run(mixed, lest::texts(1, "[hide]"), hidden) == 1);

    if (failures) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("lest: ok\n");
    return 0;
}
