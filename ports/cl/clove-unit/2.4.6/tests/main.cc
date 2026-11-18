// The C++ counterpart of main.c: CLOVE_RUNNER()'s main is renamed so the run
// gets -x and main checks that every test was discovered and run.
#define CLOVE_IMPLEMENTATION
#define CLOVE_SUITE_NAME CabinCppSuite
#include "clove-unit.h"

#include <cstdio>
#include <string>
#include <vector>

static int ran = 0;

CLOVE_TEST(Vectors) {
  ++ran;
  std::vector<int> v{1, 2, 3};
  CLOVE_SIZET_EQ(3, v.size());
  CLOVE_INT_EQ(6, v[0] + v[1] + v[2]);
}

CLOVE_TEST(Strings) {
  ++ran;
  const std::string s = std::string("cab") + "in";
  CLOVE_STRING_EQ("cabin", s.c_str());
  CLOVE_STRING_NE("clove", s.c_str());
}

CLOVE_TEST(Pointers) {
  ++ran;
  int x = 7;
  int* p = &x;
  CLOVE_NOT_NULL(p);
  CLOVE_PTR_EQ(&x, p);
}

#define main clove_main
CLOVE_RUNNER()
#undef main

int main(int, char* argv[]) {
  char opt[] = "-x";
  char* args[] = {argv[0], opt, nullptr};
  const int status = clove_main(2, args);
  if (status != 0 || ran != 3) {
    std::printf("FAIL clove-unit run: status %d, ran %d of 3\n", status, ran);
    return 1;
  }
  std::printf("clove-unit 2.4.6 C++ ok\n");
  return 0;
}
