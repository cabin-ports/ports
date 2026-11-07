// Exercises Boost.Config: the version macros, BOOST_STATIC_CONSTANT,
// BOOST_STRINGIZE/BOOST_JOIN helpers, and the compiler/platform/stdlib descriptions.
#include <boost/config.hpp>
#include <boost/version.hpp>

#include <cstdio>
#include <cstring>

// audit probe (never merged): boost_config's interface define must reach
// this consumer TU; the patch defined BOOST_ALL_NO_LIB as empty in
// user.hpp, the interface define passes it as 1 on the command line.
#ifndef BOOST_ALL_NO_LIB
#error "audit-probe: BOOST_ALL_NO_LIB is not defined in this consumer TU"
#endif

#if BOOST_VERSION != 109200
#error "expected Boost 1.92.0"
#endif

struct Answer {
  BOOST_STATIC_CONSTANT(int, value = 42);
};

BOOST_FORCEINLINE int add(int a, int b) { return a + b; }

int main() {
#ifdef BOOST_MSVC
  std::printf("audit-probe: BOOST_ALL_NO_LIB=[%s] BOOST_MSVC=%d\n", BOOST_STRINGIZE(BOOST_ALL_NO_LIB), BOOST_MSVC);
#else
  std::printf("audit-probe: BOOST_ALL_NO_LIB=[%s] BOOST_COMPILER=%s\n", BOOST_STRINGIZE(BOOST_ALL_NO_LIB), BOOST_COMPILER);
#endif
  if (BOOST_VERSION / 100000 != 1 || BOOST_VERSION / 100 % 1000 != 92) return 1;
  if (std::strcmp(BOOST_LIB_VERSION, "1_92") != 0) return 1;
  if (Answer::value != 42) return 1;
  if (std::strcmp(BOOST_STRINGIZE(BOOST_JOIN(ab, cd)), "abcd") != 0) return 1;
  if (std::strlen(BOOST_COMPILER) == 0) return 1;
  if (std::strlen(BOOST_PLATFORM) == 0) return 1;
  if (std::strlen(BOOST_STDLIB) == 0) return 1;
  if (BOOST_LIKELY(add(1, 2) == 3)) return 0;
  return 1;
}
