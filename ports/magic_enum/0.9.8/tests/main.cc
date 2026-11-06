// C++ consumer: enum reflection, string conversion, flags, containers,
// switch, fuse, iteration, and stream operators, against values the
// enums below fix.
#include <magic_enum/magic_enum.hpp>
// The umbrella header includes the other eight, magic_enum_format.hpp too.
#include <magic_enum/magic_enum_all.hpp>
#include <magic_enum/magic_enum_containers.hpp>
#include <magic_enum/magic_enum_flags.hpp>
#include <magic_enum/magic_enum_fuse.hpp>
#include <magic_enum/magic_enum_iostream.hpp>
#include <magic_enum/magic_enum_switch.hpp>
#include <magic_enum/magic_enum_utility.hpp>

#include <cstdio>
#include <sstream>
#include <string>

enum class Color : int { RED = -10, BLUE = 0, GREEN = 10 };
enum Direction { Up = 1, Down = 2 };
enum class Perm : unsigned { None = 0, Read = 1, Write = 2, Exec = 4 };

template <>
struct magic_enum::customize::enum_range<Perm> {
  static constexpr bool is_flags = true;
};

namespace {

int failures = 0;

void check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

// Reflection is compile-time: these must hold as constant expressions.
static_assert(magic_enum::enum_count<Color>() == 3);
static_assert(magic_enum::enum_name(Color::GREEN) == "GREEN");
static_assert(magic_enum::enum_values<Color>()[0] == Color::RED);

void test_reflection() {
  check(magic_enum::enum_name(Color::RED) == "RED", "enum_name scoped");
  check(magic_enum::enum_name(Down) == "Down", "enum_name unscoped");
  check(magic_enum::enum_name(static_cast<Color>(5)).empty(),
        "enum_name of a non-enumerator");
  check(magic_enum::enum_type_name<Color>() == "Color", "enum_type_name");
  const auto names = magic_enum::enum_names<Color>();
  check(names.size() == 3 && names[0] == "RED" && names[1] == "BLUE" &&
            names[2] == "GREEN",
        "enum_names in value order");
  check(magic_enum::enum_index(Color::GREEN).value_or(99) == 2, "enum_index");
  check(magic_enum::enum_integer(Color::RED) == -10, "enum_integer");
  check(magic_enum::enum_contains<Color>(10) &&
            !magic_enum::enum_contains<Color>(5),
        "enum_contains");
}

void test_cast() {
  check(magic_enum::enum_cast<Color>("BLUE") == Color::BLUE, "cast by name");
  check(!magic_enum::enum_cast<Color>("PURPLE").has_value(),
        "cast of an unknown name");
  check(magic_enum::enum_cast<Color>("green", magic_enum::case_insensitive) ==
            Color::GREEN,
        "case-insensitive cast");
  check(magic_enum::enum_cast<Color>(-10) == Color::RED, "cast by value");
  check(!magic_enum::enum_cast<Color>(1).has_value(),
        "cast of an unknown value");
}

void test_flags() {
  using namespace magic_enum::bitwise_operators;
  const Perm rx = Perm::Read | Perm::Exec;
  check(magic_enum::enum_flags_name(rx) == "Read|Exec", "enum_flags_name");
  check(magic_enum::enum_flags_cast<Perm>("Write|Read") ==
            (Perm::Read | Perm::Write),
        "enum_flags_cast by names");
  check(!magic_enum::enum_flags_cast<Perm>(8U).has_value(),
        "enum_flags_cast of an unknown bit");
  check(magic_enum::enum_flags_contains(rx) &&
            !magic_enum::enum_flags_contains(static_cast<Perm>(9)),
        "enum_flags_contains");
}

void test_containers() {
  magic_enum::containers::array<Color, int> rgb{{1, 4, 2}};
  check(rgb[Color::RED] == 1 && rgb[Color::BLUE] == 4 && rgb[Color::GREEN] == 2,
        "containers::array indexing");
  magic_enum::containers::bitset<Color> seen;
  seen.set(Color::GREEN);
  check(seen.test(Color::GREEN) && !seen.test(Color::RED) && seen.count() == 1,
        "containers::bitset");
}

void test_switch_fuse_utility() {
  const int doubled = magic_enum::enum_switch(
      [](auto c) {
        constexpr Color v = c;
        return magic_enum::enum_integer(v) * 2;
      },
      Color::GREEN, 0);
  check(doubled == 20, "enum_switch");

  const auto a = magic_enum::enum_fuse(Color::RED, Up);
  const auto b = magic_enum::enum_fuse(Color::RED, Down);
  check(a.has_value() && b.has_value() && *a != *b, "enum_fuse distinct");

  check(magic_enum::enum_next_value(Color::RED) == Color::BLUE &&
            !magic_enum::enum_next_value(Color::GREEN).has_value(),
        "enum_next_value");
  check(magic_enum::enum_prev_value_circular(Color::RED) == Color::GREEN,
        "enum_prev_value_circular");
  std::string order;
  magic_enum::enum_for_each<Color>(
      [&order](auto c) { order += magic_enum::enum_name(c.value); });
  check(order == "REDBLUEGREEN", "enum_for_each");
}

void test_streams() {
  using namespace magic_enum::iostream_operators;
  std::ostringstream out;
  out << Color::BLUE << ' ' << Color::GREEN;
  check(out.str() == "BLUE GREEN", "operator<<");
  std::istringstream in("RED");
  Color c = Color::BLUE;
  in >> c;
  check(in && c == Color::RED, "operator>>");
}

}  // namespace

int main() {
  test_reflection();
  test_cast();
  test_flags();
  test_containers();
  test_switch_fuse_utility();
  test_streams();
  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("magic_enum C++ consumer ok");
  return 0;
}
