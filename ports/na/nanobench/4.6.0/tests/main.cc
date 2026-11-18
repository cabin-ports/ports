#include <nanobench.h>

#include <cstdint>
#include <cstdio>
#include <sstream>
#include <string>

namespace nb = ankerl::nanobench;

namespace {

int failures = 0;

void check(bool ok, const char* what, int line) {
  if (!ok) {
    std::fprintf(stderr, "main.cc:%d: check failed: %s\n", line, what);
    ++failures;
  }
}

#define CHECK(...) check((__VA_ARGS__), #__VA_ARGS__, __LINE__)

void test_bench() {
  int n = 0;
  nb::Bench b;
  // Exact epoch iterations and no warmup (the default) run the op exactly epochs x iterations times.
  b.title("suite").unit("call").epochs(3).epochIterations(7).output(nullptr).run("count", [&] { ++n; });
  CHECK(n == 21);
  CHECK(b.results().size() == 1);
  nb::Result const& r = b.results()[0];
  CHECK(r.config().mBenchmarkTitle == "suite" && r.config().mBenchmarkName == "count" && r.config().mUnit == "call");
  CHECK(r.size() == 3 && r.sum(nb::Result::Measure::iterations) == 21.0);
  CHECK(r.minimum(nb::Result::Measure::iterations) == 7.0 && r.maximum(nb::Result::Measure::iterations) == 7.0);

  b.run("twice", [&] { n += 2; });
  CHECK(n == 21 + 42 && b.results().size() == 2);

  std::ostringstream rendered;
  nb::render("{{#result}}{{title}};{{name}};{{unit}}\n{{/result}}", b, rendered);
  CHECK(rendered.str() == "suite;count;call\nsuite;twice;call\n");

  std::ostringstream table;
  nb::Bench().output(&table).epochs(1).epochIterations(1).run("printed", [] {});
  CHECK(table.str().find("| `printed`") != std::string::npos);

  nb::doNotOptimizeAway(n);
}

// compare() interleaves the alternatives round by round, each epoch at the same exact iteration count.
void test_compare() {
  int a = 0;
  int b = 0;
  nb::CompareResult const cr = nb::Bench().output(nullptr).epochs(4).epochIterations(5).compare(
      "a", [&] { ++a; }, "b", [&] { ++b; });
  CHECK(cr.size() == 2 && cr[0].name == "a" && cr[1].name == "b");
  CHECK(cr.rounds() >= 4 && a == b && a == static_cast<int>(cr.rounds() * 5));
}

// Expected values from the header's splitMix64 seeding and RomuDuoJr step, re-implemented.
void test_rng() {
  nb::Rng rng(123);
  CHECK(rng() == UINT64_C(2222826058396098479));
  CHECK(rng() == UINT64_C(2602822925082020875));
  CHECK(rng.bounded(100) == 48);
  // The top 52 bits of the next output, as a fraction.
  CHECK(rng.uniform01() == 1146360355570515.0 / 4503599627370496.0);
  nb::Rng copy = rng.copy();
  CHECK(copy() == rng());
  nb::Rng restored(rng.state());
  CHECK(restored() == rng());
}

}  // namespace

int main() {
  CHECK(ANKERL_NANOBENCH_VERSION_MAJOR == 4 && ANKERL_NANOBENCH_VERSION_MINOR == 6 && ANKERL_NANOBENCH_VERSION_PATCH == 0);
  test_bench();
  test_compare();
  test_rng();
  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("nanobench 4.6.0 ok");
  return 0;
}
