// doctest from C++11: test cases, suites, subcases, templated test
// cases, exception and Approx assertions, run twice through
// doctest::Context with suite filters and a custom reporter that
// records the run's totals.  The second run's one failing assertion
// must be counted and turn the run's exit code to failure.
#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include <cstdio>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {

int factorial(int n) { return n <= 1 ? 1 : n * factorial(n - 1); }

void thrower() { throw std::runtime_error("boom"); }

int subcase_entries = 0;
doctest::TestRunStats last_run = {};

// Records the totals of each run; prints nothing.
struct Totals : doctest::IReporter {
  explicit Totals(const doctest::ContextOptions&) {}
  void report_query(const doctest::QueryData&) override {}
  void test_run_start() override {}
  void test_run_end(const doctest::TestRunStats& stats) override {
    last_run = stats;
  }
  void test_case_start(const doctest::TestCaseData&) override {}
  void test_case_reenter(const doctest::TestCaseData&) override {}
  void test_case_end(const doctest::CurrentTestCaseStats&) override {}
  void test_case_exception(const doctest::TestCaseException&) override {}
  void subcase_start(const doctest::SubcaseSignature&) override {}
  void subcase_end() override {}
  void log_assert(const doctest::AssertData&) override {}
  void log_message(const doctest::MessageData&) override {}
  void test_case_skipped(const doctest::TestCaseData&) override {}
};

}  // namespace

REGISTER_REPORTER("totals", 1, Totals);

TEST_SUITE("math") {
  TEST_CASE("factorial") {  // 4 assertions
    CHECK(factorial(0) == 1);
    CHECK(factorial(5) == 120);
    REQUIRE_EQ(factorial(10), 3628800);
    CHECK_FALSE(factorial(3) == 7);
  }
}

// Runs twice, once per subcase: 2 assertions each time.
TEST_CASE("subcases re-enter the test case") {
  ++subcase_entries;
  std::vector<int> v(3, 0);
  REQUIRE(v.size() == 3u);
  SUBCASE("push_back") {
    v.push_back(1);
    CHECK(v.size() == 4u);
  }
  SUBCASE("clear") {
    v.clear();
    CHECK(v.empty());
  }
}

TEST_CASE("exceptions") {  // 3 assertions
  CHECK_THROWS_AS(thrower(), std::runtime_error);
  CHECK_THROWS_WITH(thrower(), "boom");
  CHECK_NOTHROW(factorial(1));
}

TEST_CASE("approx") {  // 2 assertions
  CHECK(0.1 + 0.2 == doctest::Approx(0.3));
  CHECK(1.0 != doctest::Approx(1.1));
}

// Two test cases, 2 assertions each.
TEST_CASE_TEMPLATE("signed integer types", T, short, long long) {
  CHECK(sizeof(T) >= 2u);
  CHECK(std::numeric_limits<T>::is_signed);
}

TEST_SUITE("failing") {
  TEST_CASE("deliberate failure") {  // 2 assertions, 1 failing
    CHECK(factorial(3) == 7);
    CHECK(factorial(3) == 6);
  }
}

namespace {

int failures = 0;

void check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "main.cc: check failed: %s\n", what);
    ++failures;
  }
}

// Runs the registered test cases through the "totals" reporter with one
// suite filter, and returns Context::run's exit code.
int run(const char* filter, const char* suite) {
  doctest::Context context;
  context.setOption("reporters", "totals");
  context.addFilter(filter, suite);
  return context.run();
}

}  // namespace

int main() {
  check(std::strcmp(DOCTEST_VERSION_STR, "2.5.3") == 0, "DOCTEST_VERSION_STR");

  int passing = run("test-suite-exclude", "failing");
  check(passing == 0, "the passing run exits 0");
  check(last_run.numTestCases == 7, "7 test cases registered");
  check(last_run.numTestCasesPassingFilters == 6, "6 test cases selected");
  check(last_run.numTestCasesFailed == 0, "no test case failed");
  check(last_run.numAsserts == 17, "17 assertions");
  check(last_run.numAssertsFailed == 0, "no assertion failed");
  check(subcase_entries == 2, "one entry per subcase");

  int failing = run("test-suite", "failing");
  check(failing != 0, "the failing run exits non-zero");
  check(last_run.numTestCasesPassingFilters == 1, "1 test case selected");
  check(last_run.numTestCasesFailed == 1, "1 test case failed");
  check(last_run.numAsserts == 2, "2 assertions");
  check(last_run.numAssertsFailed == 1, "1 assertion failed");

  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  return 0;
}
