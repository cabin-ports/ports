#include <tscns.h>

#include <chrono>
#include <cstdio>
#include <thread>

static int failures = 0;

#define CHECK(cond)                                         \
  do {                                                      \
    if (!(cond)) {                                          \
      std::printf("FAILED line %d: %s\n", __LINE__, #cond); \
      ++failures;                                           \
    }                                                       \
  } while (0)

int main() {
  CHECK(TSCNS::NsPerSec == 1000000000);

  TSCNS tn;
  tn.init();
  CHECK(tn.getTscGhz() > 0.0);

  // tsc2ns() converts exactly with known parameters: 5000 + (1500 - 1000) * 2.
  TSCNS fixed;
  fixed.init();
  fixed.saveParam(1000, 5000, 0, 2.0);
  CHECK(fixed.tsc2ns(1500) == 6000);
  CHECK(fixed.getTscGhz() == 0.5);

  // rdns() is monotonic over a few reads; calibrate() is a no-op this soon after init().
  int64_t prev = tn.rdns();
  for (int i = 0; i < 100; i++) {
    tn.calibrate();
    int64_t cur = tn.rdns();
    CHECK(cur >= prev);
    prev = cur;
  }

  // sleep_for sleeps at least the requested time; 40 ms allows for the
  // calibration error of the 50 ms sleep.
  int64_t t0 = tn.rdns();
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  int64_t elapsed = tn.rdns() - t0;
  CHECK(elapsed >= 40000000);

  // rdns() never runs behind tsc2ns() of an earlier counter read.
  int64_t a = tn.tsc2ns(TSCNS::rdtsc());
  CHECK(tn.rdns() >= a);

  return failures ? 1 : 0;
}
