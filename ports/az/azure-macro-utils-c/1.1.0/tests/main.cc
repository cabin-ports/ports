// The counting, conditional and arithmetic macros are constant expressions,
// one field list drives both MU_DEFINE_STRUCT and a member-wise comparison,
// and enums defined inside a namespace convert to and from their names.
#include <azure_macro_utils/macro_utils.h>

#include <cstdio>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, int line, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "main.cc:%d: check failed: %s\n", line, what);
    ++failures;
  }
}

#define CHECK(expr) check((expr), __LINE__, #expr)

static_assert(MU_COUNT_ARG() == 0, "");
static_assert(MU_COUNT_ARG(a, b, c) == 3, "");
static_assert(MU_IF(0, 1, 2) == 2, "");
static_assert(MU_IF(MU_DEC(1), 1, 2) == 2, "");
static_assert(MU_IF(MU_INC(0), 1, 2) == 1, "");
static_assert(MU_DIV2(MU_COUNT_ARG(a, b, c, d)) == 2, "");

#define POINT_FIELDS int, x, int, y, double, weight
MU_DEFINE_STRUCT(POINT, POINT_FIELDS)

#define FIELD_EQUAL(type, name) &&a.name == b.name
bool equal(const POINT& a, const POINT& b) {
  return true MU_FOR_EACH_2(FIELD_EQUAL, POINT_FIELDS);
}

#define WEEKDAY_VALUES MONDAY, TUESDAY, WEDNESDAY
MU_DEFINE_ENUM(WEEKDAY, WEEKDAY_VALUES)
MU_DEFINE_ENUM_STRINGS(WEEKDAY, WEEKDAY_VALUES)

#define SIGNAL_VALUES SIGNAL_HANGUP, 1, SIGNAL_KILL, 9, SIGNAL_TERM, 15
MU_DEFINE_ENUM_2(SIGNAL, SIGNAL_VALUES)
MU_DEFINE_ENUM_STRINGS_2(SIGNAL, SIGNAL_VALUES)

#define STRINGIFY_ITEM(item) MU_TOSTRING(item),

}  // namespace

int main() {
  const POINT origin = {0, 0, 1.0};
  POINT moved = origin;
  CHECK(equal(origin, moved));
  moved.x = 1;
  CHECK(!equal(origin, moved));
  moved = origin;
  moved.y = 1;
  CHECK(!equal(origin, moved));
  moved = origin;
  moved.weight = 2.0;
  CHECK(!equal(origin, moved));

  // Every name parses back to the value it came from.
  const int weekdays = MU_ENUM_VALUE_COUNT(WEEKDAY_VALUES);
  for (int value = 0; value < weekdays; ++value) {
    const WEEKDAY day = static_cast<WEEKDAY>(value);
    WEEKDAY parsed = static_cast<WEEKDAY>((value + 1) % weekdays);
    CHECK(MU_STRING_TO_ENUM(MU_ENUM_TO_STRING(WEEKDAY, day), WEEKDAY, &parsed) == 0);
    CHECK(parsed == day);
  }
  CHECK(std::string(MU_ENUM_TO_STRING(WEEKDAY, TUESDAY)) == "TUESDAY");
  WEEKDAY parsed = MONDAY;
  CHECK(MU_STRING_TO_ENUM("FRIDAY", WEEKDAY, &parsed) != 0);
  CHECK(parsed == MONDAY);

  CHECK(SIGNAL_KILL == 9);
  CHECK(std::string(MU_ENUM_TO_STRING_2(SIGNAL, SIGNAL_TERM)) == "SIGNAL_TERM");
  CHECK(std::string(MU_ENUM_TO_STRING_2(SIGNAL, static_cast<SIGNAL>(2))) == "NULL");
  char text[32];
  std::snprintf(text, sizeof text, "%" PRI_MU_ENUM, MU_ENUM_VALUE_2(SIGNAL, SIGNAL_KILL));
  CHECK(std::string(text) == "SIGNAL_KILL (9)");

  const std::vector<std::string> names = {MU_FOR_EACH_1(STRINGIFY_ITEM, alpha, beta, gamma)};
  CHECK((names == std::vector<std::string>{"alpha", "beta", "gamma"}));

  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("azure-macro-utils-c C++ consumer test passed");
  return 0;
}
