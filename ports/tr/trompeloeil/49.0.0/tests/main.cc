// Consumer test for cabin-ports/trompeloeil, with no test framework: a
// custom reporter records each violation and throws on the fatal ones, which
// must not return.  The expectations use the C++11 API (the
// _V macros), which C++14 and later keep; the C++14 API is also exercised
// when it is available.
#include <trompeloeil.hpp>

#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

struct Store {
  virtual ~Store() = default;
  virtual int get(int key) const = 0;
  virtual void put(int key, int value) = 0;
  virtual bool erase(int key) = 0;
};

struct MockStore : Store {
  MAKE_CONST_MOCK1(get, int(int), override);
  MAKE_MOCK2(put, void(int, int), override);
  MAKE_MOCK1(erase, bool(int), override);
};

// The code under test: moves a value to another key.
static bool move_value(Store &store, int from, int to) {
  int value = store.get(from);
  if (value < 0) return false;
  store.put(to, value);
  return store.erase(from);
}

struct violation : std::runtime_error {
  explicit violation(const std::string &msg) : std::runtime_error(msg) {}
};

struct report {
  trompeloeil::severity severity;
  std::string msg;
};

static std::vector<report> reports;
static int matched_calls = 0;  // counted by the OK reporter
static int failures = 0;

static void check(bool pass, const char *what) {
  if (!pass) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

static bool contains(const std::string &text, const char *part) { return text.find(part) != std::string::npos; }

static void test_require_in_sequence() {
  MockStore store;
  trompeloeil::sequence seq;
  REQUIRE_CALL_V(store, get(1), .IN_SEQUENCE(seq).RETURN(7));
  REQUIRE_CALL_V(store, put(2, 7), .IN_SEQUENCE(seq));
  REQUIRE_CALL_V(store, erase(1), .IN_SEQUENCE(seq).RETURN(true));
  check(move_value(store, 1, 2), "move_value returns erase's result");
  check(seq.is_completed(), "the sequence is completed");
}

static void test_allow_and_forbid() {
  MockStore store;
  ALLOW_CALL_V(store, get(trompeloeil::gt(0)), .RETURN(_1 * 10));
  ALLOW_CALL_V(store, get(trompeloeil::le(0)), .RETURN(-1));
  FORBID_CALL_V(store, put(trompeloeil::_, trompeloeil::_));
  check(store.get(3) == 30 && store.get(4) == 40 && store.get(3) == 30, "ALLOW_CALL matches any number of times");
  check(!move_value(store, 0, 5), "a negative value is not moved");
}

static void test_forbidden_call_is_reported() {
  MockStore store;
  ALLOW_CALL_V(store, erase(trompeloeil::_), .RETURN(true));
  FORBID_CALL_V(store, erase(9));  // the newest matching expectation wins
  bool caught = false;
  try {
    store.erase(9);
  } catch (const violation &) {
    caught = true;
  }
  check(caught && reports.size() == 1 && reports[0].severity == trompeloeil::severity::fatal &&
            contains(reports[0].msg, "Match of forbidden call"),
        "a forbidden call reaches the reporter as a fatal violation");
  check(store.erase(8), "other keys still match ALLOW_CALL");
}

static void test_unexpected_call_is_reported() {
  MockStore store;
  bool caught = false;
  try {
    store.get(5);
  } catch (const violation &) {
    caught = true;
  }
  check(caught && reports.size() == 1 && contains(reports[0].msg, "No match for call of get"),
        "a call with no expectation reaches the reporter");
}

static void test_sequence_violation_is_reported() {
  MockStore store;
  {
    trompeloeil::sequence seq;
    REQUIRE_CALL_V(store, put(1, 1), .IN_SEQUENCE(seq));
    REQUIRE_CALL_V(store, put(2, 2), .IN_SEQUENCE(seq));
    bool caught = false;
    try {
      store.put(2, 2);
    } catch (const violation &) {
      caught = true;
    }
    check(caught && reports.size() == 1 && contains(reports[0].msg, "Sequence mismatch"),
          "an out-of-order call is a fatal violation");
    store.put(1, 1);
    store.put(2, 2);
  }
  check(reports.size() == 1, "the calls in order then satisfy the sequence");
}

static void test_unfulfilled_expectation_is_reported() {
  MockStore store;
  {
    REQUIRE_CALL_V(store, erase(3), .RETURN(true));
  }
  check(reports.size() == 1 && reports[0].severity == trompeloeil::severity::nonfatal &&
            contains(reports[0].msg, "Unfulfilled expectation"),
        "an unmet REQUIRE_CALL is reported, without throwing, when it goes out of scope");
}

#if TROMPELOEIL_CPLUSPLUS >= 201402L
static void test_cxx14_api() {
  MockStore store;
  trompeloeil::sequence seq;
  REQUIRE_CALL(store, get(1)).IN_SEQUENCE(seq).RETURN(7);
  REQUIRE_CALL(store, put(2, 7)).IN_SEQUENCE(seq);
  REQUIRE_CALL(store, erase(1)).IN_SEQUENCE(seq).RETURN(true);
  ALLOW_CALL(store, get(trompeloeil::gt(1))).RETURN(_1 + 1);
  FORBID_CALL(store, erase(2));
  check(move_value(store, 1, 2) && store.get(5) == 6, "the C++14 API");
}
#endif

using test_fn = void (*)();

// Runs a test that must report no violation; the OK reporter runs once per
// call that matches an expectation.
static void run_clean(test_fn test, int expected_matches) {
  reports.clear();
  matched_calls = 0;
  test();
  if (!reports.empty() || matched_calls != expected_matches) {
    std::fprintf(stderr, "FAIL: %d violation(s) and %d matched call(s), expected none and %d\n",
                 static_cast<int>(reports.size()), matched_calls, expected_matches);
    ++failures;
  }
}

// Runs a test that checks the violations it causes.
static void run_violating(test_fn test) {
  reports.clear();
  test();
}

int main() {
  trompeloeil::set_reporter(
      [](trompeloeil::severity severity, const char *, unsigned long, const std::string &msg) {
        reports.push_back(report{severity, msg});
        if (severity == trompeloeil::severity::fatal) throw violation(msg);
      },
      [](const char *) { ++matched_calls; });

  run_clean(test_require_in_sequence, 3);
  run_clean(test_allow_and_forbid, 4);
#if TROMPELOEIL_CPLUSPLUS >= 201402L
  run_clean(test_cxx14_api, 4);
#endif
  run_violating(test_forbidden_call_is_reported);
  run_violating(test_unexpected_call_is_reported);
  run_violating(test_sequence_violation_is_reported);
  run_violating(test_unfulfilled_expectation_is_reported);
  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("trompeloeil 49.0.0 C++ ok");
  return 0;
}
