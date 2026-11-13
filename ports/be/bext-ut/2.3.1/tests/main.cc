// bext-ut: tests run under a counting reporter, so both directions are
// checked: the expectations that pass, and one that fails on purpose and is
// counted without failing the process.
#include <boost/ut.hpp>

#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

namespace ut = boost::ut;

namespace {

int failures = 0;

void check(bool ok, int line, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "main.cc:%d: check failed: %s\n", line, what);
    ++failures;
  }
}

#define CHECK(expr) check(static_cast<bool>(expr), __LINE__, #expr)

// What the reporter saw, in order.
struct Tally {
  std::vector<std::string> tests;
  std::vector<std::string> failed_tests;
  int passed = 0;
  int failed = 0;
  std::string failure;
  int failure_line = 0;
} tally;

class Reporter {
 public:
  auto on(ut::events::run_begin) -> void {}
  auto on(ut::events::test_begin begin) -> void {
    tally.tests.emplace_back(begin.name);
    failed_before_ = tally.failed;
  }
  auto on(ut::events::test_run) -> void {}
  auto on(ut::events::test_skip) -> void {}
  auto on(ut::events::test_end end) -> void {
    if (tally.failed > failed_before_) {
      tally.failed_tests.emplace_back(end.name);
    }
  }
  template <class TMsg>
  auto on(ut::events::log<TMsg>) -> void {}
  template <class TExpr>
  auto on(ut::events::assertion_pass<TExpr>) -> void {
    ++tally.passed;
  }
  template <class TExpr>
  auto on(ut::events::assertion_fail<TExpr> fail) -> void {
    ++tally.failed;
    // ut's own printer, without the default colour escapes.
    ut::printer text{ut::colors{.none = "", .pass = "", .fail = "", .skip = ""}};
    text << fail.expr;
    tally.failure = text.str();
    tally.failure_line = static_cast<int>(fail.location.line());
  }
  auto on(ut::events::fatal_assertion) -> void {}
  auto on(ut::events::exception) -> void { ++tally.failed; }
  auto on(ut::events::summary) -> void {}

 private:
  int failed_before_ = 0;
};

}  // namespace

template <>
auto ut::cfg<ut::override> = ut::runner<Reporter>{};

int main() {
  using namespace ut;

  "arithmetic"_test = [] {
    expect(eq(6_i, 2 * 3));
    expect(neq(4_i, 5));
    expect(42_i == 6 * 7);
  };

  "exceptions"_test = [] {
    expect(throws<std::runtime_error>(
        [] { throw std::runtime_error{"boom"}; }));
    expect(throws([] { throw 1; }));
    expect(nothrow([] {}));
  };

  int sum = 0;
  "squares"_test = [&sum](int value) {
    sum += value * value;
    expect(eq(value * value % 2, value % 2));
  } | std::vector{1, 2, 3};

  int expected_line = 0;
  "deliberate failure"_test = [&expected_line] {
    expect(throws<std::runtime_error>([] { throw 1; }));
    expected_line = __LINE__ + 1;
    expect(eq(2 + 2, 5));
    expect(eq(2 + 2, 4));
  };

  // run() reports whether any expectation failed; having called it, the
  // runner does not exit with a failure status at the end of the program.
  CHECK(cfg<override>.run());
  CHECK((tally.tests ==
         std::vector<std::string>{"arithmetic", "exceptions", "squares (1)",
                                  "squares (2)", "squares (3)",
                                  "deliberate failure"}));
  CHECK((tally.failed_tests == std::vector<std::string>{"deliberate failure"}));
  CHECK(tally.passed == 10 && tally.failed == 2);
  CHECK(tally.failure == "4 == 5");
  CHECK(tally.failure_line == expected_line);
  CHECK(sum == 14);

  if (failures != 0) {
    return 1;
  }
  std::printf("bext-ut ok\n");
  return 0;
}
