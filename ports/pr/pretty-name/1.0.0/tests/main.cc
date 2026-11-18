// Checks pretty-name's names exactly where GCC, Clang and MSVC spell a type the
// same way, and by its stable part where they differ (cabin.toml).

#include <pretty-name/pretty_name.hpp>

#include <cstdio>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

namespace demo {
struct widget {};
} // namespace demo

static int failures = 0;

static void check(bool ok, const char *what, const std::string &got) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s (got \"%s\")\n", what, got.c_str());
    ++failures;
  }
}

static bool ends_with(const std::string &s, const std::string &suffix) {
  return s.size() >= suffix.size() &&
         s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

constexpr static_string int_name = type_name<int>();
static_assert(int_name.size() == 3 && int_name[0] == 'i' && int_name[2] == 't',
              "type_name works in a constant expression");

int main() {
  std::string name = pretty_name::pretty_name<int>();
  check(name == "int", "pretty_name<int>", name);
  name = pretty_name::pretty_name<unsigned>();
  check(name == "unsigned int", "pretty_name<unsigned>", name);

  std::vector<std::string> names = pretty_name::pretty_names(std::tuple<int, double>{});
  check(names == std::vector<std::string>{"int", "double"}, "pretty_names(tuple<int, double>)",
        names.empty() ? std::string() : names.front());
  check(pretty_name::pretty_names(std::tuple<>{}).empty(), "pretty_names(tuple<>)", "");

  // MSVC prefixes "struct " and "class ", and spells out default template
  // arguments.
  name = pretty_name::pretty_name<demo::widget>();
  check(ends_with(name, "demo::widget"), "pretty_name<demo::widget>", name);
  name = pretty_name::pretty_name<std::vector<int>>();
  check(name.find("std::vector<int") != std::string::npos, "pretty_name<std::vector<int>>", name);

  std::ostringstream os;
  os << type_name<char>();
  check(os.str() == "char", "operator<<(ostream, static_string)", os.str());
  name = static_to_string(type_name<double>());
  check(name == "double", "static_to_string", name);

  bool threw = false;
  try {
    (void)int_name[3];
  } catch (const std::out_of_range &) {
    threw = true;
  }
  check(threw, "static_string::operator[] past the end throws", "");

  if (failures != 0) {
    return 1;
  }
  std::puts("pretty-name 1.0.0 C++ ok");
  return 0;
}
