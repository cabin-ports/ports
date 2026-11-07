// Consumer test for cabin-ports/debug-assert: module handlers, assertion
// levels, handler arguments, DEBUG_UNREACHABLE in a constant expression,
// that an assertion above the module's level never evaluates its expression,
// and that abort() follows the default handler.
#include <debug_assert.hpp>

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {

int failures = 0;

void check(bool ok, const char* what) {
  if (!ok) {
    std::printf("FAILED: %s\n", what);
    ++failures;
  }
}

struct failure {
  std::string file;
  unsigned line;
  std::string expression;
  std::string message;
};

// Checks assertions up to level 2 and throws instead of returning, so a
// failed assertion never reaches abort().
struct checked : debug_assert::set_level<2>, debug_assert::allow_exception {
  static void handle(const debug_assert::source_location& loc,
                     const char* expression, const char* message = "") {
    throw failure{loc.file_name, loc.line_number, expression, message};
  }
};

// A handler with its own argument type, forwarded as-is.
struct coded : debug_assert::set_level<1>, debug_assert::allow_exception {
  static void handle(const debug_assert::source_location&, const char*,
                     int code) {
    throw code;
  }
};

// Level 0 disables every assertion of the module.
struct off : debug_assert::no_handler, debug_assert::set_level<0> {};

// The default handler returns after printing, so abort() must follow it.
struct defaulted : debug_assert::default_handler, debug_assert::set_level<1> {};

int evaluated = 0;

bool counted(bool value) {
  ++evaluated;
  return value;
}

constexpr int checked_div(int a, int b) {
  return b != 0 ? a / b : DEBUG_UNREACHABLE(checked{}, "division by zero");
}

static_assert(checked_div(6, 3) == 2,
              "DEBUG_UNREACHABLE is usable in a constant expression");

// Runs f and reports whether it threw a failure, which it stores in *out.
template <class F>
bool fails(F f, failure* out) {
  try {
    f();
  } catch (const failure& e) {
    *out = e;
    return true;
  }
  return false;
}

}  // namespace

// Reached only through the final assertion's abort(); C linkage, as C++11
// requires of signal handlers.
extern "C" void on_abort(int) {
  std::printf("debug-assert (C++): %s\n", failures == 0 ? "ok" : "FAILED");
  std::fflush(stdout);
  std::_Exit(failures == 0 ? 0 : 1);
}

int main() {
  failure f{};
  int x = 1;
  unsigned line = 0;

  check(!fails([&] { DEBUG_ASSERT(x > 0, checked{}); }, &f),
        "a true assertion does not call the handler");

  x = 0;
  check(fails([&] { line = __LINE__; DEBUG_ASSERT(x > 0, checked{}); }, &f),
        "a false assertion calls the handler");
  check(f.expression == "x > 0", "the handler gets the stringified expression");
  check(f.line == line, "the handler gets the line of the assertion");
  check(std::strstr(f.file.c_str(), "main.cc") != nullptr,
        "the handler gets the file of the assertion");
  check(f.message.empty(), "no message without extra arguments");

  evaluated = 0;
  check(fails([&] { DEBUG_ASSERT(counted(x > 0), checked{}, "x must be positive"); }, &f),
        "a false assertion with a message calls the handler");
  check(evaluated == 1, "an assertion is evaluated once");
  check(f.message == "x must be positive", "extra arguments reach the handler");

  evaluated = 0;
  check(!fails([&] { DEBUG_ASSERT(counted(false), checked{}, debug_assert::level<3>{}); }, &f),
        "an assertion above the module level is not checked");
  check(evaluated == 0, "an assertion above the module level is not evaluated");

  check(fails([&] { DEBUG_ASSERT(counted(false), checked{}, debug_assert::level<2>{}, "level 2"); }, &f),
        "an assertion at the module level is checked");
  check(evaluated == 1, "an assertion at the module level is evaluated once");
  check(f.expression == "counted(false)" && f.message == "level 2",
        "a level comes before the handler arguments");

  evaluated = 0;
  DEBUG_ASSERT(counted(false), off{});
  check(evaluated == 0, "a level 0 module neither evaluates nor fails");

  int code = 0;
  try {
    DEBUG_ASSERT(x == 1, coded{}, 42);
  } catch (int c) {
    code = c;
  }
  check(code == 42, "a handler's own argument types are forwarded");

  check(checked_div(7, 2) == 3, "DEBUG_UNREACHABLE is not reached at run time");
  check(fails([] { checked_div(1, 0); }, &f), "DEBUG_UNREACHABLE calls the handler");
  check(f.expression.empty() && f.message == "division by zero",
        "DEBUG_UNREACHABLE passes an empty expression and the message");
  check(std::strstr(f.file.c_str(), "main.cc") != nullptr,
        "DEBUG_UNREACHABLE passes its source location");

  // Last, so no earlier std::terminate() reaches the SIGABRT handler.
  std::signal(SIGABRT, on_abort);
  DEBUG_ASSERT(x > 0, defaulted{}, "expected: the test ends in abort()");
  std::printf("FAILED: abort() follows a handler that returns\n");
  return 1;
}
