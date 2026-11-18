#include <bitflags/bitflags.hpp>

#include <cstdint>
#include <cstdio>
#include <string>
#include <type_traits>

namespace {

int failures = 0;

#define CHECK(cond)                                                            \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__,    \
                   #cond);                                                     \
      ++failures;                                                              \
    }                                                                          \
  } while (0)

} // namespace

// Each flag's bit comes from its line's distance to BEGIN_*, so the FLAG
// lines stay consecutive; the first one is 0.
BEGIN_BITFLAGS(Flags)
FLAG(none)
FLAG(flag_a)
FLAG(flag_b)
FLAG(flag_c)
END_BITFLAGS(Flags)

BEGIN_RAW_BITFLAGS(RawFlags)
RAW_FLAG(none)
RAW_FLAG(a)
RAW_FLAG(b)
END_RAW_BITFLAGS(RawFlags)

// Before C++17 the odr-used flags need these definitions (README.md:170-181).
DEFINE_FLAG(Flags, none)
DEFINE_FLAG(Flags, flag_a)
DEFINE_FLAG(Flags, flag_b)
DEFINE_FLAG(Flags, flag_c)
DEFINE_FLAG(RawFlags, none)
DEFINE_FLAG(RawFlags, a)
DEFINE_FLAG(RawFlags, b)

static_assert(std::is_same<Flags::underlying_type, std::uint8_t>::value,
              "four flags fit in std::uint8_t");

int main() {
  CHECK(Flags::none.bits == 0);
  CHECK(Flags::flag_a.bits == 1);
  CHECK(Flags::flag_b.bits == 2);
  CHECK(Flags::flag_c.bits == 4);
  // name is std::string_view or char const *, depending on the standard.
  CHECK(std::string(Flags::flag_b.name) == "flag_b");

  Flags f = Flags::flag_a | Flags::flag_b;
  CHECK(f.bits() == 3);
  CHECK(f.contains(Flags::flag_a));
  CHECK(!f.contains(Flags::flag_c));
  CHECK(f.contains(Flags::flag_a, Flags::flag_b));
  f.toggle(Flags::flag_c);
  CHECK(f.bits() == 7);
  f.remove(Flags::flag_a);
  CHECK(f.bits() == 6);
  f.toggle(Flags::flag_c);
  CHECK(f.bits() == 2);
  f.clear();
  CHECK(f.is_empty());

  CHECK(RawFlags::none.bits == 0);
  CHECK(static_cast<std::uint8_t>(RawFlags::a) == 1);
  CHECK(static_cast<std::uint8_t>(RawFlags::b) == 2);

  // README Quick Start idiom: bitflags & flag, read through operator T().
  Flags g = Flags::flag_a | Flags::flag_c;
  CHECK(static_cast<bool>(g & Flags::flag_a));
  CHECK(!(g & Flags::flag_b));
  CHECK(!g.contains(Flags::flag_a, Flags::flag_b));  // README.md:257
  CHECK(g.contains(Flags::none));  // a zero flag is always contained
  g.remove(Flags::flag_b);  // not set: no change
  CHECK(g.bits() == 5);
  g.set(Flags::flag_b);
  CHECK(g == (Flags::flag_a | Flags::flag_b | Flags::flag_c));
  CHECK(!g.is_all());  // is_all() means every bit of underlying_type
  g = Flags::all();
  CHECK(g.is_all() && !g.is_empty());

  RawFlags r = RawFlags::a | RawFlags::b;
  r.remove(RawFlags::a);
  CHECK(r.bits() == 2);
  CHECK(r.contains(RawFlags::b) && !r.contains(RawFlags::a));

  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("bitflags ok");
  return 0;
}
