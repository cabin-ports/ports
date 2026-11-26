/* A C program using the C interface of CppUTest and CppUTestExt: the
 * tracked allocator wrappers with their call counter, and the mock C API's
 * expectation bookkeeping, which needs no running test case. */
#include <stdio.h>
#include <string.h>

#include "CppUTest/TestHarness_c.h"
#include "CppUTestExt/MockSupport_c.h"

static int failures;

#define CHECK_THAT(expr)                                          \
  do {                                                            \
    if (!(expr)) {                                                \
      fprintf(stderr, "main.c:%d: check failed: %s\n", __LINE__, #expr); \
      ++failures;                                                 \
    }                                                             \
  } while (0)

int main(void) {
  void* p;
  char* s;

  cpputest_malloc_count_reset();
  p = cpputest_malloc(16);
  CHECK_THAT(p != NULL);
  CHECK_THAT(cpputest_malloc_get_count() == 1);
  s = cpputest_strdup("cabin");
  CHECK_THAT(s != NULL && strcmp(s, "cabin") == 0);
  CHECK_THAT(cpputest_malloc_get_count() == 2);
  cpputest_free(s);
  cpputest_free(p);

  mock_c()->expectOneCall("f")->withIntParameters("x", 1);
  CHECK_THAT(mock_c()->expectedCallsLeft() == 1);
  mock_c()->actualCall("f")->withIntParameters("x", 1);
  mock_c()->checkExpectations();

  mock_c()->expectNCalls(2, "g");
  mock_c()->actualCall("g");
  mock_c()->actualCall("g");
  mock_c()->checkExpectations();

  mock_c()->expectOneCall("h");
  CHECK_THAT(mock_c()->expectedCallsLeft() == 1);
  mock_c()->clear();
  CHECK_THAT(mock_c()->expectedCallsLeft() == 0);

  return failures != 0;
}
