// CppUTest's runner executes the test cases below on a custom argv.  A
// TestTestingFixture runs a deliberately failing case inside a case that
// passes, and the outer case checks the nested run's counts and failure text;
// CppUTestExt's MockSupport is checked the same way for a match and for two
// kinds of mismatch.  main exits 0 only if the runner reports no failure and
// every case really ran.
#include <cstdio>

#include "CppUTest/CommandLineTestRunner.h"
#include "CppUTest/TestHarness.h"
#include "CppUTest/TestTestingFixture.h"
#include "CppUTestExt/MockSupport.h"

namespace {
int cases_run = 0;

void passing() { CHECK_EQUAL(2, 1 + 1); }
void failing_equal() { CHECK_EQUAL(1, 2); }
void failing_strcmp() { STRCMP_EQUAL("abc", "abd"); }
void mock_wrong_parameter() {
  mock().expectOneCall("f").withParameter("x", 1);
  mock().actualCall("f").withParameter("x", 2);
}
void mock_unfulfilled() {
  mock().expectOneCall("g");
  mock().checkExpectations();
}
}  // namespace

TEST_GROUP(Strings) {};

TEST(Strings, SubStringAndSize) {
  ++cases_run;
  SimpleString s("abcdef");
  STRCMP_EQUAL("cd", s.subString(2, 2).asCharString());
  LONGS_EQUAL(6, s.size());
  CHECK(s.contains("def"));
  STRCMP_EQUAL("4294967295", StringFrom(4294967295u).asCharString());
}

TEST_GROUP(Nested) {};

TEST(Nested, PassingRun) {
  ++cases_run;
  TestTestingFixture f;
  f.setTestFunction(passing);
  f.runAllTests();
  LONGS_EQUAL(1, f.getRunCount());
  LONGS_EQUAL(0, f.getFailureCount());
  LONGS_EQUAL(1, f.getCheckCount());
}

TEST(Nested, FailingCheckEqual) {
  ++cases_run;
  TestTestingFixture f;
  f.setTestFunction(failing_equal);
  f.runAllTests();
  LONGS_EQUAL(1, f.getRunCount());
  LONGS_EQUAL(1, f.getFailureCount());
  f.assertPrintContains("expected <1>");
  f.assertPrintContains("but was  <2>");
  f.assertPrintContains("Failure in TEST(");
}

TEST(Nested, FailingStrcmp) {
  ++cases_run;
  TestTestingFixture f;
  f.setTestFunction(failing_strcmp);
  f.runAllTests();
  LONGS_EQUAL(1, f.getFailureCount());
  f.assertPrintContains("expected <abc>");
  f.assertPrintContains("but was  <abd>");
}

TEST_GROUP(Mock) {
  void teardown() { mock().clear(); }
};

TEST(Mock, MatchingCallPasses) {
  ++cases_run;
  mock().expectOneCall("f").withParameter("x", 1);
  mock().actualCall("f").withParameter("x", 1);
  mock().checkExpectations();
}

TEST(Mock, WrongParameterIsDetected) {
  ++cases_run;
  TestTestingFixture f;
  f.setTestFunction(mock_wrong_parameter);
  f.runAllTests();
  LONGS_EQUAL(1, f.getFailureCount());
  f.assertPrintContains("Unexpected parameter value to parameter \"x\" to function \"f\"");
}

TEST(Mock, UnfulfilledExpectationIsDetected) {
  ++cases_run;
  TestTestingFixture f;
  f.setTestFunction(mock_unfulfilled);
  f.runAllTests();
  LONGS_EQUAL(1, f.getFailureCount());
  f.assertPrintContains("Expected call WAS NOT fulfilled");
}

int main(int, char**) {
  const char* av[] = {"cpputest-consumer", "-v"};
  int failures = CommandLineTestRunner::RunAllTests(2, av);
  if (failures != 0) return 1;
  if (cases_run != 7) {
    std::fprintf(stderr, "expected 7 cases to run, ran %d\n", cases_run);
    return 1;
  }
  return 0;
}
