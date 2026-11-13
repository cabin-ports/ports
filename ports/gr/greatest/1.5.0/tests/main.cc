// A C++98 consumer of greatest with GREATEST_USE_ABBREVS off: the
// GREATEST_-prefixed API, a std::string type info, a fixture object reset by
// setup and counted by teardown, a parametric test, and the report after one
// deliberate failure, which prints a FAIL line.
#define GREATEST_USE_ABBREVS 0
#include <greatest.h>

#include <cstdio>
#include <string>
#include <vector>

namespace {

int failures = 0;

#define EXPECT(cond)                                              \
  do {                                                            \
    if (!(cond)) {                                                \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      ++failures;                                                 \
    }                                                             \
  } while (0)

int string_equal(const void* expd, const void* got, void*) {
  return *static_cast<const std::string*>(expd) ==
         *static_cast<const std::string*>(got);
}

int string_print(const void* t, void*) {
  return std::printf("\"%s\"", static_cast<const std::string*>(t)->c_str());
}

greatest_type_info string_type_info = {string_equal, string_print};

struct Fixture {
  std::vector<int> values;
  int setups;
  int teardowns;
  Fixture() : setups(0), teardowns(0) {}
};

Fixture fixture;
bool reached_after_failure = false;

void fixture_setup(void* udata) {
  Fixture* f = static_cast<Fixture*>(udata);
  f->values.clear();
  f->values.push_back(3);
  f->values.push_back(1);
  f->values.push_back(2);
  ++f->setups;
}

void fixture_teardown(void* udata) {
  ++static_cast<Fixture*>(udata)->teardowns;
}

GREATEST_TEST sum_then_grow(void) {
  int sum = 0;
  for (std::vector<int>::size_type i = 0; i < fixture.values.size(); ++i) {
    sum += fixture.values[i];
  }
  GREATEST_ASSERT_EQ(6, sum);
  GREATEST_ASSERT_EQ(3u, fixture.values.size());
  fixture.values.push_back(4);
  GREATEST_PASS();
}

GREATEST_TEST setup_resets(void) {
  GREATEST_ASSERT_EQ_FMT(3, static_cast<int>(fixture.values.size()), "%d");
  GREATEST_PASS();
}

GREATEST_TEST std_string(void) {
  const std::string expected("greatest 1.5.0");
  std::string got("greatest ");
  got += "1.5.0";
  GREATEST_ASSERT_EQUAL_T(&expected, &got, &string_type_info, NULL);
  GREATEST_ASSERT_STR_EQ("1.5.0", got.c_str() + 9);
  GREATEST_PASS();
}

struct Scaled {
  double value;
  double factor;
  double expected;
};

GREATEST_TEST scaled(void* env) {
  const Scaled* s = static_cast<const Scaled*>(env);
  GREATEST_ASSERT_IN_RANGE(s->expected, s->value * s->factor, 1e-12);
  GREATEST_PASS();
}

GREATEST_TEST skipped(void) { GREATEST_SKIPm("nothing to check"); }

GREATEST_TEST deliberate_failure(void) {
  const std::string a("left");
  const std::string b("right");
  GREATEST_ASSERT_EQUAL_Tm("strings differ", &a, &b, &string_type_info, NULL);
  reached_after_failure = true;
  GREATEST_PASS();
}

GREATEST_SUITE(fixtures) {
  Scaled s = {1.5, 4.0, 6.0};
  GREATEST_SET_SETUP_CB(fixture_setup, &fixture);
  GREATEST_SET_TEARDOWN_CB(fixture_teardown, &fixture);
  GREATEST_RUN_TEST(sum_then_grow);
  GREATEST_RUN_TEST(setup_resets);
  GREATEST_RUN_TEST(std_string);
  GREATEST_RUN_TEST1(scaled, &s);
  GREATEST_RUN_TEST(skipped);
  GREATEST_RUN_TEST(deliberate_failure);
}

}  // namespace

GREATEST_MAIN_DEFS();

int main() {
  greatest_report_t report;

  GREATEST_INIT();
  GREATEST_RUN_SUITE(fixtures);
  GREATEST_PRINT_REPORT();
  greatest_get_report(&report);

  EXPECT(greatest_info.tests_run == 6);
  EXPECT(report.passed == 4);
  EXPECT(report.failed == 1);
  EXPECT(report.skipped == 1);
  EXPECT(report.assertions == 7);
  EXPECT(!greatest_all_passed());
  EXPECT(!reached_after_failure);
  EXPECT(fixture.setups == 6);
  EXPECT(fixture.teardowns == 6);

  if (failures != 0) {
    std::printf("%d failures\n", failures);
    return 1;
  }
  std::printf("greatest (C++): ok\n");
  return 0;
}
