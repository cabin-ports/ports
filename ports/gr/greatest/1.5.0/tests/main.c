/* A C89 consumer of greatest: the assertion macros, a custom type, setup
 * and teardown callbacks, a parametric test, a helper through CHECK_CALL,
 * name exclusion, shuffling, and the report after one deliberate failure.
 * The expected failure prints a FAIL line; main checks the counts instead
 * of returning GREATEST_MAIN_END's status. */
#include <greatest.h>

#include <stdio.h>
#include <string.h>

static int failures = 0;

#define EXPECT(cond)                                         \
  do {                                                       \
    if (!(cond)) {                                           \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      ++failures;                                            \
    }                                                        \
  } while (0)

struct point {
  int x;
  int y;
};

static int point_equal(const void* expd, const void* got, void* udata) {
  const struct point* a = (const struct point*)expd;
  const struct point* b = (const struct point*)got;
  (void)udata;
  return a->x == b->x && a->y == b->y;
}

static int point_print(const void* t, void* udata) {
  const struct point* p = (const struct point*)t;
  (void)udata;
  return printf("(%d, %d)", p->x, p->y);
}

static greatest_type_info point_type_info = {point_equal, point_print};

static int setups = 0;
static int teardowns = 0;
static int reached_after_failure = 0;
static char order[4];
static size_t order_len = 0;

static void count_call(void* udata) { ++*(int*)udata; }

TEST relational(void) {
  ASSERT(1 + 1 == 2);
  ASSERT_FALSE(1 > 2);
  ASSERT_EQ(4, 2 + 2);
  ASSERT_NEQ(4, 5);
  ASSERT_GT(3, 2);
  ASSERT_GTE(3, 3);
  ASSERT_LT(2, 3);
  ASSERT_LTE(3, 3);
  ASSERT_EQ_FMT(42, 6 * 7, "%d");
  PASS();
}

TEST strings_and_memory(void) {
  char buf[8];
  sprintf(buf, "%s%d", "ab", 12);
  ASSERT_STR_EQ("ab12", buf);
  ASSERT_STRN_EQ("ab99", buf, 2);
  ASSERT_MEM_EQ("ab1", buf, 3);
  ASSERT_IN_RANGE(0.3, 0.1 + 0.2, 1e-12);
  PASS();
}

TEST custom_type(void) {
  struct point a = {1, 2};
  struct point b = {1, 2};
  ASSERT_EQUAL_T(&a, &b, &point_type_info, NULL);
  PASS();
}

static enum greatest_test_res check_x(const struct point* p, int x) {
  ASSERT_EQ(x, p->x);
  PASS();
}

TEST via_helper(void) {
  struct point p = {5, 6};
  CHECK_CALL(check_x(&p, 5));
  PASS();
}

TEST skipped(void) { SKIPm("nothing to check"); }

TEST deliberate_failure(void) {
  struct point a = {1, 2};
  struct point b = {2, 1};
  ASSERT_EQUAL_Tm("points differ", &a, &b, &point_type_info, NULL);
  reached_after_failure = 1;
  PASS();
}

struct range {
  int lo;
  int hi;
};

TEST ordered_range(void* env) {
  const struct range* r = (const struct range*)env;
  ASSERT_LT(r->lo, r->hi);
  PASS();
}

TEST excluded_failure(void) { FAILm("excluded by name"); }

TEST mark_a(void) {
  order[order_len++] = 'a';
  PASS();
}

TEST mark_b(void) {
  order[order_len++] = 'b';
  PASS();
}

TEST mark_c(void) {
  order[order_len++] = 'c';
  PASS();
}

SUITE(basics) {
  SET_SETUP(count_call, &setups);
  SET_TEARDOWN(count_call, &teardowns);
  RUN_TEST(relational);
  RUN_TEST(strings_and_memory);
  RUN_TEST(custom_type);
  RUN_TEST(via_helper);
  RUN_TEST(skipped);
  RUN_TEST(deliberate_failure);
}

SUITE(runner) {
  struct range r = {1, 9};
  RUN_TEST1(ordered_range, &r);
  greatest_set_test_exclude("excluded");
  RUN_TEST(excluded_failure);
  greatest_set_test_exclude(NULL);
  SHUFFLE_TESTS(12345, {
    RUN_TEST(mark_a);
    RUN_TEST(mark_b);
    RUN_TEST(mark_c);
  });
}

GREATEST_MAIN_DEFS();

int main(void) {
  struct greatest_report_t report;

  GREATEST_INIT();
  RUN_SUITE(basics);
  RUN_SUITE(runner);
  GREATEST_PRINT_REPORT();
  greatest_get_report(&report);

  EXPECT(GREATEST_VERSION_MAJOR == 1 && GREATEST_VERSION_MINOR == 5 &&
         GREATEST_VERSION_PATCH == 0);
  EXPECT(greatest_info.tests_run == 10);
  EXPECT(report.passed == 8);
  EXPECT(report.failed == 1);
  EXPECT(report.skipped == 1);
  EXPECT(report.assertions == 17);
  EXPECT(!greatest_all_passed());
  EXPECT(!reached_after_failure);
  EXPECT(setups == 6);
  EXPECT(teardowns == 6);
  /* Seed 12345 over three tests fixes this order on every target
   * (greatest.h:1109-1138). */
  EXPECT(order_len == 3);
  EXPECT(memcmp(order, "acb", 3) == 0);

  if (failures != 0) {
    printf("%d failures\n", failures);
    return 1;
  }
  printf("greatest (C): ok\n");
  return 0;
}
