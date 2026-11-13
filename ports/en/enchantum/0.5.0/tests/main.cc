#include <enchantum/array.hpp>
#include <enchantum/bitflags.hpp>
#include <enchantum/bitset.hpp>
#include <enchantum/bitwise_operators.hpp>
#include <enchantum/enchantum.hpp>
#include <enchantum/iostream.hpp>
#include <enchantum/next_value.hpp>
#include <enchantum/scoped.hpp>
#include <enchantum/type_name.hpp>

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

enum class Color { Red, Green, Blue };

// Unscoped, without a fixed underlying type.
enum Fruit { Apple = 1, Banana, Cherry };

namespace app {

// Sparse and signed, with a negative value.
enum class Level : std::int8_t { Trace = -2, Debug = 0, Info = 3, Error = 100 };

enum class Perm : std::uint8_t { None = 0, Read = 1 << 0, Write = 1 << 1, Exec = 1 << 2 };
ENCHANTUM_DEFINE_BITWISE_FOR(Perm)

// Beyond the default reflection range of [-256, 256].
enum class Status : std::uint16_t { Ok = 200, NotFound = 404, Teapot = 418, Internal = 500 };

}  // namespace app

template <>
struct enchantum::enum_traits<app::Status> {
  static constexpr auto min = 0;
  static constexpr auto max = 512;
};

static_assert(enchantum::count<Color> == 3);
static_assert(enchantum::to_string(Color::Blue) == "Blue");
static_assert(enchantum::cast<Color>("Green") == Color::Green);
static_assert(enchantum::is_bitflag<app::Perm>);
static_assert(!enchantum::is_bitflag<Color>);

namespace {

int failures = 0;

void check(bool ok, const char* what, int line) {
  if (!ok) {
    std::fprintf(stderr, "main.cc:%d: check failed: %s\n", line, what);
    ++failures;
  }
}

#define CHECK(...) check((__VA_ARGS__), #__VA_ARGS__, __LINE__)

bool iequal(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) return false;
  for (std::size_t i = 0; i < a.size(); ++i)
    if (std::tolower(static_cast<unsigned char>(a[i])) !=
        std::tolower(static_cast<unsigned char>(b[i])))
      return false;
  return true;
}

void test_scoped() {
  CHECK(enchantum::to_string(Color::Green) == "Green");
  CHECK(enchantum::to_string(static_cast<Color>(42)).empty());

  CHECK(enchantum::cast<Color>("Blue") == Color::Blue);
  CHECK(!enchantum::cast<Color>("Purple").has_value());
  CHECK(!enchantum::cast<Color>("blue").has_value());
  CHECK(enchantum::cast<Color>("gREEN", iequal) == Color::Green);
  CHECK(enchantum::cast<Color>(2) == Color::Blue);
  CHECK(!enchantum::cast<Color>(3).has_value());

  CHECK(enchantum::contains<Color>("Red"));
  CHECK(!enchantum::contains<Color>("Purple"));
  CHECK(enchantum::contains(Color::Red));
  CHECK(!enchantum::contains(static_cast<Color>(-1)));

  const auto& names = enchantum::names<Color>;
  CHECK(names.size() == 3 && names[0] == "Red" && names[1] == "Green" && names[2] == "Blue");
  const auto& values = enchantum::values<Color>;
  CHECK(values.size() == 3 && values[0] == Color::Red && values[2] == Color::Blue);
  const auto& entries = enchantum::entries<Color>;
  CHECK(entries[1].first == Color::Green && entries[1].second == "Green");

  CHECK(enchantum::min<Color> == Color::Red);
  CHECK(enchantum::max<Color> == Color::Blue);
  CHECK(enchantum::is_contiguous<Color>);
  CHECK(enchantum::enum_to_index(Color::Blue) == std::size_t{2});
  CHECK(enchantum::index_to_enum<Color>(1) == Color::Green);
  CHECK(!enchantum::index_to_enum<Color>(3).has_value());

  CHECK(enchantum::next_value(Color::Red) == Color::Green);
  CHECK(!enchantum::next_value(Color::Blue).has_value());
  CHECK(enchantum::prev_value(Color::Blue, 2) == Color::Red);
  CHECK(enchantum::next_value_circular(Color::Blue) == Color::Red);
  CHECK(enchantum::prev_value_circular(Color::Red) == Color::Blue);
}

void test_unscoped() {
  CHECK(enchantum::count<Fruit> == 3);
  CHECK(enchantum::to_string(Banana) == "Banana");
  CHECK(enchantum::cast<Fruit>("Cherry") == Cherry);
  CHECK(enchantum::cast<Fruit>(1) == Apple);
  CHECK(!enchantum::contains<Fruit>(0));
  CHECK(enchantum::min<Fruit> == Apple && enchantum::max<Fruit> == Cherry);
}

void test_sparse() {
  using app::Level;
  CHECK(enchantum::count<Level> == 4);
  CHECK(!enchantum::is_contiguous<Level>);
  CHECK(enchantum::min<Level> == Level::Trace);
  CHECK(enchantum::max<Level> == Level::Error);
  CHECK(enchantum::to_string(Level::Trace) == "Trace");
  CHECK(enchantum::to_string(Level::Error) == "Error");
  CHECK(enchantum::cast<Level>(std::int8_t{-2}) == Level::Trace);
  CHECK(!enchantum::cast<Level>(std::int8_t{1}).has_value());
  CHECK(!enchantum::contains(static_cast<Level>(-1)));
  CHECK(enchantum::enum_to_index(Level::Info) == std::size_t{2});
  CHECK(enchantum::index_to_enum<Level>(3) == Level::Error);
  CHECK(enchantum::next_value(Level::Debug) == Level::Info);
}

void test_traits() {
  using app::Status;
  CHECK(enchantum::count<Status> == 4);
  CHECK(enchantum::to_string(Status::NotFound) == "NotFound");
  CHECK(enchantum::to_string(Status::Internal) == "Internal");
  CHECK(enchantum::cast<Status>("Teapot") == Status::Teapot);
  CHECK(enchantum::cast<Status>(std::uint16_t{200}) == Status::Ok);
  CHECK(!enchantum::cast<Status>(std::uint16_t{201}).has_value());
  CHECK(enchantum::max<Status> == Status::Internal);
}

void test_bitflags() {
  using app::Perm;
  // A bitflag enum reflects 0 and the powers of two.
  CHECK(enchantum::count<Perm> == 4);
  CHECK(enchantum::to_string_bitflag(Perm::Read | Perm::Exec) == "Read|Exec");
  CHECK(enchantum::to_string_bitflag(Perm::Exec | Perm::Write, ',') == "Write,Exec");
  CHECK(enchantum::to_string_bitflag(Perm::None) == "None");
  CHECK(enchantum::to_string_bitflag(static_cast<Perm>(8)).empty());

  CHECK(enchantum::cast_bitflag<Perm>("Write|Read") == (Perm::Read | Perm::Write));
  CHECK(enchantum::cast_bitflag<Perm>("read,EXEC", ',', iequal) == (Perm::Read | Perm::Exec));
  CHECK(!enchantum::cast_bitflag<Perm>("Write|Delete").has_value());
  CHECK(enchantum::cast_bitflag<Perm>(std::uint8_t{7}) == (Perm::Read | Perm::Write | Perm::Exec));
  CHECK(!enchantum::cast_bitflag<Perm>(std::uint8_t{9}).has_value());

  CHECK(enchantum::contains_bitflag(Perm::Read | Perm::Write));
  CHECK(!enchantum::contains_bitflag<Perm>(std::uint8_t{16}));
  CHECK(enchantum::contains_bitflag<Perm>("Read|Exec"));
  CHECK(!enchantum::contains_bitflag<Perm>("Read|Delete"));
}

void test_containers() {
  using app::Level;
  enchantum::array<Level, int> hits{};
  hits[Level::Trace] = 1;
  hits[Level::Error] = 4;
  hits.at(Level::Info) = 3;
  CHECK(hits.size() == 4);
  CHECK(hits[0] == 1 && hits[1] == 0 && hits[2] == 3 && hits[3] == 4);
  CHECK(hits[Level::Error] == 4);
  bool threw = false;
  try {
    (void)hits.at(static_cast<Level>(1));
  } catch (const std::out_of_range&) {
    threw = true;
  }
  CHECK(threw);

  enchantum::bitset<Level> seen{Level::Debug, Level::Error};
  CHECK(seen.size() == 4 && seen.count() == 2);
  CHECK(seen.test(Level::Debug) && !seen.test(Level::Info));
  seen.set(Level::Trace).reset(Level::Error);
  CHECK(seen.to_string() == "Trace|Debug");
  CHECK(seen.to_string(',') == "Trace,Debug");
  CHECK(seen.to_string('0', '1') == "0011");
  seen.flip(Level::Info);
  CHECK(seen[Level::Info]);
  threw = false;
  try {
    seen.set(static_cast<Level>(1));
  } catch (const std::out_of_range&) {
    threw = true;
  }
  CHECK(threw);
}

void test_type_names() {
  CHECK(enchantum::raw_type_name<app::Level> == "app::Level");
  CHECK(enchantum::type_name<app::Level> == "Level");
  CHECK(enchantum::type_name<Color> == "Color");
  CHECK(enchantum::scoped::to_string(app::Level::Info) == "Level::Info");
  CHECK(enchantum::scoped::cast<app::Level>("Level::Error") == app::Level::Error);
  CHECK(!enchantum::scoped::cast<app::Level>("Error").has_value());
  CHECK(enchantum::scoped::contains<Color>("Color::Red"));
  CHECK(!enchantum::scoped::contains<Color>("Fruit::Red"));
}

void test_iostream() {
  using namespace enchantum::iostream_operators;

  std::ostringstream out;
  out << Color::Green << ' ' << static_cast<Color>(300) << ' ' << (app::Perm::Read | app::Perm::Write) << ' '
      << static_cast<app::Perm>(8);
  CHECK(out.str() == "Green 300 Read|Write 8");

  std::istringstream in("Blue Exec|Read Purple");
  Color color = Color::Red;
  app::Perm perm = app::Perm::None;
  in >> color >> perm;
  CHECK(in && color == Color::Blue && perm == (app::Perm::Read | app::Perm::Exec));
  in >> color;
  CHECK(in.fail() && color == Color::Blue);
}

}  // namespace

int main() {
  test_scoped();
  test_unscoped();
  test_sparse();
  test_traits();
  test_bitflags();
  test_containers();
  test_type_names();
  test_iostream();
  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("enchantum 0.5.0 ok");
  return 0;
}
