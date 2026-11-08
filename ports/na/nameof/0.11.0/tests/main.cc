// The C++ consumer: nameof in C++17.  Member and pointer names are checked
// only where upstream supports them (not MSVC before C++20).  Type spellings
// are compiler-specific, so only spellings upstream's own tests expect alike
// on GCC, Clang and MSVC are compared, and otherwise only the parts they
// share.
#include <nameof.hpp>

#include <cstdio>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>

static int failures = 0;

#define CHECK(cond)                                                   \
  do {                                                                \
    if (!(cond)) {                                                    \
      std::fprintf(stderr, "main.cc:%d: check failed: %s\n", __LINE__, \
                   #cond);                                            \
      ++failures;                                                     \
    }                                                                 \
  } while (0)

namespace app::detail {
enum class Color { RED = -10, GREEN = 0, BLUE = 10 };
enum Flags : unsigned {
  HasClaws = 1u << 0,
  CanFly = 1u << 1,
  EatsFish = 1u << 2
};
// 300 lies outside the default range [-128, 127].
enum class Big { small = 1, large = 300 };
enum class Named { plain, custom };
// A C-style enum without a fixed underlying type.
enum Legacy { First = 1, Second = 2, Fourth = 4 };

template <class T>
struct Box {
  T value;
  int size() const { return 1; }
};
struct Point {
  int x;
  int y;
};
struct Plain {};
struct Base {
  virtual ~Base() = default;
};
struct Derived : Base {};

template <class A, class B>
int pick() {
  return 0;
}
}  // namespace app::detail

int global_counter = 0;

template <>
struct nameof::customize::enum_range<app::detail::Big> {
  static constexpr int min = 0;
  static constexpr int max = 300;
};

template <>
constexpr std::string_view nameof::customize::enum_name<app::detail::Named>(
    app::detail::Named value) noexcept {
  return value == app::detail::Named::custom ? "the custom one"
                                             : std::string_view();
}

template <>
constexpr std::string_view
nameof::customize::type_name<app::detail::Plain>() noexcept {
  return "PlainType";
}

using namespace app::detail;

static void expression_names() {
  int answer = 42;
  Box<int> box{7};
  const auto variable = NAMEOF(answer);
  CHECK(variable == std::string_view("answer"));
  CHECK(NAMEOF(box.value) == std::string_view("value"));
  CHECK(NAMEOF(box.size()) == std::string_view("size"));
  CHECK(NAMEOF(pick<int, float>()) == std::string_view("pick"));
  CHECK(NAMEOF_FULL(pick<int, float>()) ==
        std::string_view("pick<int, float>"));
  CHECK(NAMEOF(__LINE__) == std::string_view("__LINE__"));
  CHECK(NAMEOF_RAW(::app::detail::Color::RED) ==
        std::string_view("::app::detail::Color::RED"));
  (void)answer;
}

static void enum_names() {
  static_assert(nameof::nameof_enum<Color::BLUE>() == std::string_view("BLUE"));

  Color color = Color::RED;
  CHECK(NAMEOF_ENUM(color) == "RED");
  CHECK(nameof::nameof_enum(Color::BLUE) == "BLUE");
  CHECK(NAMEOF_ENUM(static_cast<Color>(5)).empty());
  CHECK(NAMEOF_ENUM_OR(static_cast<Color>(5), "none") == "none");
  CHECK(NAMEOF_ENUM_OR(Color::GREEN, "none") == "GREEN");
  CHECK(NAMEOF_ENUM_CONST(Color::GREEN) == std::string_view("GREEN"));

  const auto flags = static_cast<Flags>(CanFly | EatsFish);
  CHECK(NAMEOF_ENUM_FLAG(flags) == "CanFly|EatsFish");
  CHECK(nameof::nameof_enum_flag(flags, '$') == "CanFly$EatsFish");
  // One unnamed bit empties the whole name.
  CHECK(NAMEOF_ENUM_FLAG(static_cast<Flags>(CanFly | (1u << 3))).empty());
  CHECK(NAMEOF_ENUM(static_cast<Flags>(HasClaws | CanFly)).empty());

  // Found at run time only because enum_range widens the scan to 300 (C++26
  // standard reflection, which ignores the range, is not used at C++17).
  Big big = Big::large;
  CHECK(NAMEOF_ENUM(big) == "large");
  CHECK(nameof::nameof_enum<Big::large>() == std::string_view("large"));

  CHECK(NAMEOF_ENUM(Named::custom) == "the custom one");
  CHECK(NAMEOF_ENUM(Named::plain) == "plain");  // an empty custom name

  Legacy legacy = Second;
  CHECK(NAMEOF_ENUM(legacy) == "Second");
  CHECK(NAMEOF_ENUM(static_cast<Legacy>(3)).empty());
}

static void type_names() {
  int answer = 0;
  CHECK(NAMEOF_TYPE(const int&) == std::string_view("int"));
  CHECK(NAMEOF_TYPE_EXPR(answer) == std::string_view("int"));
  CHECK(NAMEOF_SHORT_TYPE(const Box<int>&) == std::string_view("Box"));
  CHECK(NAMEOF_SHORT_TYPE(Color) == std::string_view("Color"));
  CHECK(NAMEOF_TYPE(Plain) == std::string_view("PlainType"));
  CHECK(NAMEOF_FULL_TYPE(Plain) == std::string_view("PlainType"));
  // "const app::detail::Point &" or "struct app::detail::Point const &".
  const std::string_view full = NAMEOF_FULL_TYPE(const Point&);
  CHECK(full.find("Point") != std::string_view::npos &&
        full.find("const") != std::string_view::npos && full.back() == '&');
  (void)answer;

#if defined(NAMEOF_TYPE_RTTI_SUPPORTED)
  Derived derived;
  const Base& base = derived;
  CHECK(NAMEOF_SHORT_TYPE_RTTI(base) == "Derived");
  // "const app::detail::Derived&", or with "struct " on MSVC.
  const std::string rtti = NAMEOF_FULL_TYPE_RTTI(base);
  CHECK(rtti.rfind("const ", 0) == 0 && rtti.size() > 8 &&
        rtti.compare(rtti.size() - 8, 8, "Derived&") == 0);
#endif
}

static void member_and_pointer_names() {
#if defined(NAMEOF_MEMBER_SUPPORTED)
  CHECK(NAMEOF_MEMBER(&Point::y) == std::string_view("y"));
  CHECK(nameof::nameof_member<&Point::x>() == std::string_view("x"));
  CHECK(nameof::nameof_member<static_cast<int Point::*>(nullptr)>() ==
        std::string_view("nullptr"));
#else
  std::puts("NAMEOF_MEMBER unsupported (it needs C++20 on MSVC)");
#endif
#if defined(NAMEOF_POINTER_SUPPORTED)
  CHECK(NAMEOF_POINTER(&global_counter) == std::string_view("global_counter"));
#else
  std::puts("NAMEOF_POINTER unsupported (it needs C++20 on MSVC)");
#endif
}

static void cstrings() {
  int counter = 0;
  int another = 0;
  const auto name = NAMEOF(counter);
  CHECK(name.size() == 7 && name.str() == "counter");
  CHECK(std::string_view(name.c_str()) == "counter");
  CHECK(name == NAMEOF(counter) && !(name == NAMEOF(another)));
  std::ostringstream out;
  out << std::setw(9) << std::setfill('.') << name;
  CHECK(out.str() == "..counter");
  (void)counter;
  (void)another;
}

int main() {
  expression_names();
  enum_names();
  type_names();
  member_and_pointer_names();
  cstrings();
  if (failures != 0) return 1;
  std::puts("nameof C++ consumer test passed");
  return 0;
}
