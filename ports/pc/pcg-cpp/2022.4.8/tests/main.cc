#include <pcg_random.hpp>

#include <cstdint>
#include <cstdio>
#include <numeric>
#include <random>
#include <sstream>
#include <string>

namespace {

int failures = 0;

void expect(bool ok, const char* rng, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "check failed: %s: %s\n", rng, what);
    ++failures;
  }
}

// Round 1 of upstream's test-high/pcg-test.cpp (seed 42, stream 54), whose
// output test-high/expected/check-<rng>.out records.
template <class RNG>
void check_round1(const char* name, const typename RNG::result_type (&nums)[6], const char* coins,
                  const char* rolls, const char* cards) {
  RNG rng(42u, 54u);
  bool same = true;
  for (auto n : nums) same = same && rng() == n;
  expect(same, name, "first six outputs");
  rng.backstep(6);
  same = true;
  for (auto n : nums) same = same && rng() == n;
  expect(same, name, "outputs after backstep(6)");

  std::string c;
  for (int i = 0; i < 65; ++i) c += rng(2) ? 'H' : 'T';
  expect(c == coins, name, "coins");

  const RNG before_rolls = rng;
  std::string r;
  for (int i = 0; i < 33; ++i) r += ' ' + std::to_string(static_cast<unsigned>(rng(6)) + 1);
  expect(r == rolls, name, "rolls");
  // operator- returns the state type; cl's emulated 128-bit one compares only with its own type.
  expect(rng - before_rolls == typename RNG::state_type(33u), name, "distance after rolls");

  char deck[52];
  std::iota(deck, deck + 52, 0);
  pcg_extras::shuffle(deck, deck + 52, rng);
  std::string d;
  for (int card : deck) {
    d += ' ';
    d += "A23456789TJQK"[card / 4];
    d += "hcds"[card % 4];
  }
  expect(d == cards, name, "shuffled deck");
}

template <class RNG>
void check_jumps_and_streaming(const char* name) {
  RNG a(42u, 54u), b(42u, 54u);
  const RNG start = a;
  a.advance(1000);
  for (int i = 0; i < 1000; ++i) b();
  expect(a == b && a - start == typename RNG::state_type(1000u), name, "advance(1000) equals 1000 calls");
  b.discard(10);
  a.advance(10);
  expect(a == b, name, "discard equals advance");
  a.backstep(1010);
  expect(a == start, name, "backstep returns to the start");

  std::stringstream state;
  state << b;
  RNG restored;
  state >> restored;
  expect(!state.fail() && restored == b && restored() == b(), name, "stream round trip");

  RNG reseeded;
  reseeded.seed(42u, 54u);
  expect(reseeded == start, name, "seed(42, 54)");

  RNG moved(42u, 54u);
  moved.set_stream(7u);
  expect(moved.stream() == typename RNG::state_type(7u) && !(moved == start), name, "set_stream");
}

}  // namespace

int main() {
  const pcg32::result_type pcg32_nums[6] = {0xa15c02b7, 0x7b47f409, 0xba1d3330, 0x83d2f293, 0xbfa4784b, 0xcbed606e};
  check_round1<pcg32>(
      "pcg32", pcg32_nums, "HHTTTHTHHHTHTTTHHHHHTTTHHHTHTHTHTTHTTTHHHHHHTTTTHHTTTTTHTTTTTTTHT",
      " 3 4 1 1 2 2 3 2 4 3 2 4 3 3 5 2 3 1 3 1 5 1 4 1 5 6 4 6 6 2 6 3 3",
      " Qd Ks 6d 3s 3d 4c 3h Td Kc 5c Jh Kd Jd As 4s 4h Ad Th Ac Jc 7s Qs 2s 7h Kh 2d 6c Ah 4d Qh 9h 6s 5s 2c 9c Ts"
      " 8d 9s 3c 8c Js 5d 2h 6h 7d 8s 9d 5h 8h Qc 7c Tc");

  // pcg64 has 128-bit state: __uint128_t on GCC and Clang, upstream's emulation on cl.
  const pcg64::result_type pcg64_nums[6] = {0x86b1da1d72062b68, 0x1304aa46c9853d39, 0xa3670e9e0dd50358,
                                            0xf9090e529a7dae00, 0xc85b9fd837996f2c, 0x606121f8e3919196};
  check_round1<pcg64>(
      "pcg64", pcg64_nums, "TTTHHHTTTHHHTTTTHHTTHHTHTHTTHHTHTTTTHHTTTHTHHTHTTTTHHTTTHHHTTTHTT",
      " 6 4 1 5 1 5 5 3 6 3 4 6 2 3 6 5 5 5 1 5 3 6 2 6 1 4 4 3 5 2 6 3 2",
      " 3d 7d 3h Qd 9d 8c Ts Ad 9s 6c Jh Ac 5s 4c 2c 7s Kh Kd 7h Qh 6d Qc 8d Qs 6s Js 4d Kc 9h 3c 2h Td 5d 5h 9c 4s"
      " 5c 7c 3s 4h As Th 6h Jc 2s Jd Tc Ah 2d Ks 8h 8s");

  // An extended generator: pcg32 output mixed with a two-entry table.
  const pcg32_k2::result_type k2_nums[6] = {0x23ad45d5, 0x6e2b9c53, 0x23cf9e33, 0x24e90350, 0x7a160dc4, 0x5cfeb7ad};
  check_round1<pcg32_k2>(
      "pcg32_k2", k2_nums, "TTTTHTTTTHTTHTHTTHTTHTTTTTTTTHHHTHHTHTHTTHTHHTTHTHTTTHTHTHTTTHHHT",
      " 3 4 4 6 5 5 5 6 6 1 6 6 6 3 4 5 6 5 3 3 6 4 5 2 1 2 1 1 4 4 4 2 5",
      " 8c 9s 4d 5s As 9c 2s Tc 7c Ks 5d Td 2d 7h 8h 2h Ah 6s 2c Qs Jd Kc 4c 8s Jc 5h 6d 4h 6c Kh Th Kd Js 9d 7d Qd"
      " 6h Qc Jh 3s 9h Ac 3c Ts 7s Qh 5c Ad 3h 4s 8d 3d");

  check_jumps_and_streaming<pcg32>("pcg32");
  check_jumps_and_streaming<pcg64>("pcg64");

  expect(pcg32::period_pow2() == 64 && pcg32::streams_pow2() == 63, "pcg32", "period and streams");
  expect(pcg64::period_pow2() == 128 && pcg64::streams_pow2() == 127, "pcg64", "period and streams");

  // Seeding from a SeedSeq is deterministic, and the engines drive <random> distributions.
  std::seed_seq seq{1u, 2u, 3u};
  pcg32 x(seq), y(seq);
  std::uniform_int_distribution<int> die(1, 6);
  bool same_rolls = true;
  for (int i = 0; i < 20; ++i) same_rolls = same_rolls && die(x) == die(y);
  expect(same_rolls && !(pcg32(seq) == pcg32(42u, 54u)), "pcg32", "seeded from std::seed_seq");

  // Upstream's samples seed with entropy this way; only that it builds and runs is checked.
  pcg_extras::seed_seq_from<std::random_device> entropy;
  pcg64 fresh(entropy);
  fresh();

  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("pcg-cpp 2022.4.8 ok");
  return 0;
}
