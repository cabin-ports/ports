#include "XoshiroCpp.hpp"

#include <cstdint>
#include <cstdio>

using namespace XoshiroCpp;

static int failures = 0;

#define CHECK(cond)                                              \
  do {                                                           \
    if (!(cond)) {                                               \
      std::printf("%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
      ++failures;                                                \
    }                                                            \
  } while (0)

// The first output of Xoshiro256PlusPlus(12345), usable in a constant expression.
constexpr std::uint64_t constexprFirst() {
  Xoshiro256PlusPlus rng(12345);
  return rng();
}
static_assert(constexprFirst() == UINT64_C(10201931350592234856));
static_assert(DoubleFromBits(UINT64_C(1) << 63) == 0.5);

int main() {
  // README.md:49-53: the first five outputs for seed 12345.
  Xoshiro256PlusPlus rng(12345);
  CHECK(rng() == UINT64_C(10201931350592234856));
  CHECK(rng() == UINT64_C(3780764549115216544));
  CHECK(rng() == UINT64_C(1570246627180645737));
  CHECK(rng() == UINT64_C(3237956550421933520));
  CHECK(rng() == UINT64_C(4899705286669081817));

  // Not in the README: the reference splitmix64.c step (prng.di.unimi.it)
  // evaluated by hand for state 12345.
  SplitMix64 sm(12345);
  CHECK(sm() == UINT64_C(2454886589211414944));
  CHECK(sm() == UINT64_C(3778200017661327597));

  // README.md:147 prints 0.553048 for DoubleFromBits of the first output;
  // the exact values are the 53-bit scaling of 0 and of the top bit.
  Xoshiro256PlusPlus rng2(12345);
  const double d = DoubleFromBits(rng2());
  CHECK(d > 0.5530475 && d < 0.5530485);
  CHECK(DoubleFromBits(UINT64_C(0)) == 0.0);
  CHECK(DoubleFromBits(UINT64_C(1) << 63) == 0.5);
  CHECK(FloatFromBits(UINT32_C(1) << 31) == 0.5f);

  CHECK(Xoshiro256PlusPlus::min() == 0);
  CHECK(Xoshiro256PlusPlus::max() == UINT64_MAX);

  // README.md:201-209: outputs for SplitMix64-expanded seeds and the serialize/deserialize round trip.
  const Xoshiro256Plus::state_type initial = {
      SplitMix64{111}(), SplitMix64{222}(), SplitMix64{333}(), SplitMix64{444}()};
  Xoshiro256PlusPlus a(initial);
  CHECK(a() == UINT64_C(9228892280983206813));
  CHECK(a() == UINT64_C(11892737616047535485));
  CHECK(a() == UINT64_C(12786908792686548306));
  Xoshiro256PlusPlus b(a.serialize());
  for (int i = 0; i < 3; ++i) CHECK(a() == b());
  b.deserialize(initial);
  CHECK(b() == UINT64_C(9228892280983206813));
  CHECK(b() == UINT64_C(11892737616047535485));
  CHECK(b() == UINT64_C(12786908792686548306));

  if (failures == 0) std::puts("ok");
  return failures == 0 ? 0 : 1;
}
