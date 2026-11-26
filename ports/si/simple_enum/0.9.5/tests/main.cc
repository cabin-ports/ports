// C++ consumer: enum_name at compile time and run time, enum_cast from a name, and
// out-of-range behavior on an enum bounded by its own first and last members.
#include <simple_enum/enum_cast.hpp>
#include <simple_enum/simple_enum.hpp>

#include <cstdio>
#include <string_view>

enum struct color { red, green, blue, first = red, last = blue };

static_assert(simple_enum::enum_name(color::green) == "green");

namespace {

int failures = 0;

void check(bool ok, char const * what) {
  if (!ok) {
    std::printf("FAIL: %s\n", what);
    ++failures;
  }
}

}  // namespace

int main() {
  color const green = color::green;
  check(simple_enum::enum_name(green) == "green", "enum_name at run time");
  check(simple_enum::enum_name(color::red) == "red", "enum_name red");
  check(simple_enum::enum_name(color::blue) == "blue", "enum_name blue");

  auto const blue = simple_enum::enum_cast<color>("blue");
  check(blue.has_value() && *blue == color::blue, "enum_cast of a known name");

  auto const unknown = simple_enum::enum_cast<color>("purple");
  check(!unknown.has_value() && unknown.error() == simple_enum::enum_cast_error::invalid_cast,
        "enum_cast of an unknown name is invalid_cast");

  check(simple_enum::enum_name(static_cast<color>(7)).empty(),
        "enum_name of an out-of-range value is empty");

  return failures == 0 ? 0 : 1;
}
