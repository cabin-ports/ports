// Exercises Convert and To: string conversions through a string, an explicit
// operator std::string, operator<<, and pointers, plus the primary template's
// cast. Cases mirror upstream's cpp/Platform.Converters.Tests/
// Platform.Converters.Tests.cpp:30-60, minus the void* case, whose pointer text is
// platform-dependent.

#include <Platform.Converters.h>

#include <cstdio>
#include <iostream>
#include <string>

using Platform::Converters::Convert;
using Platform::Converters::To;

struct A {
  explicit operator std::string() const { return "A"; }
};

struct B {
  friend std::ostream &operator<<(std::ostream &out, const B &) {
    return out << "B";
  }
};

struct X {};

static int failures = 0;

static void check(bool ok, const char *what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

int main() {
  A a;
  X x;

  check((Convert<int, std::string>(1)) == "1", "Convert int to string");
  check(To<std::string>(std::string("")) == "", "string to string");
  check(To<std::string>(A()) == "A", "explicit operator std::string");
  check(To<std::string>(B()) == "B", "operator<<");
  check((Convert<A &, std::string>(a)) == "A", "Convert from reference");

  const std::string pointer = To<std::string>(&a);
  check(pointer.starts_with("pointer <"), "pointer prefix");
  check(pointer.ends_with("> to <A>"), "pointer to A suffix");
  check(To<std::string>(&x).starts_with("pointer <"), "pointer to X prefix");
  check((Convert<X *, std::string>(nullptr)) == "null pointer",
        "typed null pointer");
  check(To<std::string>(nullptr) == "null pointer", "nullptr_t");

  // Converter.h's primary template is a plain cast.
  check(To<double>(3) == 3.0, "int to double");
  check(To<int>(3.9) == 3, "double to int truncates");

  return failures == 0 ? 0 : 1;
}
