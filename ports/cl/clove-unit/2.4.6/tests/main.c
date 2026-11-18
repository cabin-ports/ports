/* CLOVE_RUNNER() defines main(), which runs the tests it finds in the
 * executable's own symbol table but exits 0 on a failed test unless given -x
 * (README.md:340).  It is renamed so main can pass -x and check that every
 * test was discovered and run.  The port test is one file, so the suite and
 * the runner share it, with CLOVE_IMPLEMENTATION and the include first. */
#define CLOVE_IMPLEMENTATION
#define CLOVE_SUITE_NAME CabinSuite
#include "clove-unit.h"

#include <stdio.h>

static int ran = 0;

CLOVE_TEST(Integers) {
  ++ran;
  CLOVE_INT_EQ(3, 1 + 2);
  CLOVE_SIZET_LT(sizeof(char), sizeof(long long));
}

CLOVE_TEST(Strings) {
  ++ran;
  CLOVE_STRING_EQ("cabin", "cabin");
  CLOVE_STRING_NE("cabin", "clove");
}

CLOVE_TEST(Floats) {
  ++ran;
  CLOVE_DOUBLE_EQ(0.5, 1.0 / 2.0);
  CLOVE_IS_TRUE(0.1f < 0.2f);
}

#define main clove_main
CLOVE_RUNNER()
#undef main

int main(int argc, char* argv[]) {
  char opt[] = "-x";
  char* args[] = {argv[0], opt, NULL};
  int status;
  (void)argc;
  status = clove_main(2, args);
  if (status != 0 || ran != 3) {
    printf("FAIL clove-unit run: status %d, ran %d of 3\n", status, ran);
    return 1;
  }
  printf("clove-unit 2.4.6 C ok\n");
  return 0;
}
