/* Consumer test for cabin-ports/fff from C++: value and void fakes, argument
 * capture, the global call history, return sequences, and resets, following
 * the README's examples. */
#include <fff.h>

#include <stdio.h>

DEFINE_FFF_GLOBALS

FAKE_VALUE_FUNC(int, get_value, int)
FAKE_VOID_FUNC(do_thing, int, char)

static int failures = 0;

static void check(int pass, const char *what) {
  if (!pass) {
    fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

int main(void) {
  int seq[] = {1, 2, 3};
  int a, b, c, d;

  get_value_fake.return_val = 42;
  check(get_value(7) == 42, "a value fake returns return_val");
  check(get_value_fake.call_count == 1 && get_value_fake.arg0_val == 7, "a value fake records its argument");

  do_thing(3, 'x');
  check(do_thing_fake.call_count == 1 && do_thing_fake.arg0_val == 3 && do_thing_fake.arg1_val == 'x',
        "a void fake records its arguments");
  check(fff.call_history_idx == 2 && fff.call_history[0] == (fff_function_t)get_value &&
            fff.call_history[1] == (fff_function_t)do_thing,
        "the call history keeps the call order");

  RESET_FAKE(get_value);
  FFF_RESET_HISTORY();
  check(get_value_fake.call_count == 0 && fff.call_history_idx == 0, "resets clear the fake and the history");

  SET_RETURN_SEQ(get_value, seq, 3);
  a = get_value(10);
  b = get_value(11);
  c = get_value(12);
  d = get_value(13);
  check(a == 1 && b == 2 && c == 3 && d == 3, "a return sequence repeats its last value");
  check(get_value_fake.call_count == 4 && get_value_fake.arg0_history[0] == 10 &&
            get_value_fake.arg0_history[3] == 13 && get_value_fake.return_val_history[3] == 3,
        "argument and return value histories");

  if (failures != 0) {
    fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  puts("fff 1.1.0 C++ ok");
  return 0;
}
