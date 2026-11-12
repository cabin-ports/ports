/* Mocks the two dependencies of a small function under test, one taking a
   struct registered with the one-argument REGISTER_UMOCK_VALUE_TYPE, and
   checks that umock_c matches expected calls, returns SetReturn and global
   mock values, and reports mismatched calls on both sides. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "umock_c/umock_c.h"

typedef struct POINT_TAG {
  int x;
  int y;
} POINT;

#define ENABLE_MOCKS
#include "umock_c/umock_c_prod.h"
MOCKABLE_FUNCTION(, int, read_sensor, int, channel);
MOCKABLE_FUNCTION(, void, plot, POINT, p);
#undef ENABLE_MOCKS

/* The code under test. */
static int plot_reading(int channel, int x) {
  int value = read_sensor(channel);
  if (value < 0) {
    return -1;
  }
  POINT p = {x, value};
  plot(p);
  return 0;
}

/* The names the one-argument REGISTER_UMOCK_VALUE_TYPE(POINT) derives. */
static char *umock_stringify_POINT(const POINT *value) {
  char *result = malloc(32);
  if (result != NULL) {
    snprintf(result, 32, "{%d,%d}", value->x, value->y);
  }
  return result;
}

static int umock_are_equal_POINT(const POINT *left, const POINT *right) {
  return left->x == right->x && left->y == right->y;
}

static int umock_copy_POINT(POINT *destination, const POINT *source) {
  *destination = *source;
  return 0;
}

static void umock_free_POINT(POINT *value) { (void)value; }

static int umock_errors;

static void on_umock_c_error(UMOCK_C_ERROR_CODE error_code) {
  fprintf(stderr, "umock_c error %d\n", (int)error_code);
  ++umock_errors;
}

static int check_calls(const char *when, const char *expected,
                       const char *actual) {
  const char *e = umock_c_get_expected_calls();
  const char *a = umock_c_get_actual_calls();
  if (e == NULL || a == NULL || strcmp(e, expected) != 0 ||
      strcmp(a, actual) != 0) {
    fprintf(stderr, "%s: expected calls \"%s\", actual calls \"%s\"\n", when,
            e != NULL ? e : "(null)", a != NULL ? a : "(null)");
    return 1;
  }
  return 0;
}

int main(void) {
  if (umock_c_init(on_umock_c_error) != 0) {
    fprintf(stderr, "umock_c_init failed\n");
    return 1;
  }
  REGISTER_UMOCK_VALUE_TYPE(POINT);
  REGISTER_GLOBAL_MOCK_RETURN(read_sensor, -1);

  POINT expected_point = {7, 42};
  STRICT_EXPECTED_CALL(read_sensor(3)).SetReturn(42);
  STRICT_EXPECTED_CALL(plot(expected_point));
  if (plot_reading(3, 7) != 0) {
    fprintf(stderr, "read_sensor did not return its SetReturn value\n");
    return 1;
  }
  if (check_calls("matched", "", "")) return 1;

  /* An unmatched call returns the global mock return, so plot is skipped. */
  umock_c_reset_all_calls();
  STRICT_EXPECTED_CALL(read_sensor(3)).SetReturn(42);
  if (plot_reading(4, 7) != -1) {
    fprintf(stderr, "read_sensor did not return the global mock return\n");
    return 1;
  }
  if (check_calls("wrong channel", "[read_sensor(3)]", "[read_sensor(4)]")) {
    return 1;
  }

  /* Struct arguments compare through the registered umock_are_equal_POINT. */
  umock_c_reset_all_calls();
  expected_point.x = 9;
  expected_point.y = 5;
  STRICT_EXPECTED_CALL(read_sensor(1)).SetReturn(5);
  STRICT_EXPECTED_CALL(plot(expected_point));
  if (plot_reading(1, 8) != 0) return 1;
  if (check_calls("wrong point", "[plot({9,5})]", "[plot({8,5})]")) return 1;

  umock_c_deinit();
  if (umock_errors != 0) return 1;
  printf("umock_c: matched, wrong channel and wrong point calls ok\n");
  return 0;
}
