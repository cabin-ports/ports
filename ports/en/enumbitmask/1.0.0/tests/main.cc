// Exercises every operator DEFINE_BITMASK_OPERATORS generates, including the
// EnumWrapper results of & | ^ and their constexpr use.
#include <EnumBitmask.hpp>

#include <cstdint>
#include <cstdio>

namespace t {
enum class Flag : std::uint8_t {
  Mask_0 = 0,
  Mask_1 = 1,
  Mask_2 = 2,
  Mask_4 = 4,
  Mask_32 = 32,
};
DEFINE_BITMASK_OPERATORS(Flag)
}  // namespace t

using t::Flag;

static_assert((Flag{11} | Flag{22}) == Flag{11 | 22});
static_assert((Flag{11} & Flag{22}) == Flag{11 & 22});
static_assert((Flag{11} ^ Flag{22}) == Flag{11 ^ 22});
static_assert(~Flag{11} == Flag{static_cast<std::uint8_t>(~11)});

namespace {

bool ok = true;

void check(bool pass, const char* what) {
  if (!pass) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ok = false;
  }
}

unsigned v(Flag f) { return static_cast<unsigned>(f); }

}  // namespace

int main() {
  constexpr Flag a = Flag::Mask_1 | Flag::Mask_2;
  check(v(a) == 3, "or");
  check(v(Flag(a & Flag::Mask_2)) == 2, "and");
  check(v(Flag(Flag{7} ^ Flag{4})) == 3, "xor");
  check(v(~Flag::Mask_0) == 255, "not");

  Flag e = Flag::Mask_1;
  e |= Flag::Mask_32;
  check(v(e) == 33, "or-assign");
  Flag f = Flag{255};
  f &= Flag::Mask_32;
  check(v(f) == 32, "and-assign");
  Flag g = Flag::Mask_1;
  g ^= Flag::Mask_32;
  check(v(g) == 33, "xor-assign");

  check(!(Flag::Mask_1 & Flag::Mask_2), "wrapper false when no common bit");
  check(static_cast<bool>(a & Flag::Mask_1), "wrapper true when bit set");
  check(!static_cast<bool>(Flag::Mask_0 | Flag::Mask_0), "wrapper false for zero");

  return ok ? 0 : 1;
}
