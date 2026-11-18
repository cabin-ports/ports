#include <SI/electric_charge.h>
#include <SI/length.h>
#include <SI/mass.h>
#include <SI/stream.h>
#include <SI/time.h>

#include <cstdint>
#include <cstdio>
#include <sstream>
#include <string>
#include <type_traits>

using namespace SI::literals;

static_assert((1_km).as<SI::metre_t>().value() == 1000, "1 km is 1000 m");
static_assert(std::is_same_v<decltype(5.0_A * 2.0_s), SI::coulomb_t<long double>>,
              "ampere times second is coulomb");
static_assert((5.0_A * 2.0_s).value() == 10.0, "5 A * 2 s is 10 C");
static_assert((1_kg).as<SI::gram_t>().value() == 1000, "1 kg is 1000 g");
static_assert((2_t).as<SI::kilo_gram_t>().value() == 2000, "2 t is 2000 kg");
static_assert((2_h).as<SI::seconds_t>().value() == 7200, "2 h is 7200 s");
static_assert(1_km == 1000_m, "comparison across ratios");

static int failures = 0;

static void check(bool ok, const char *what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

int main() {
  std::ostringstream os;
  os << 10_km;
  check(os.str() == "10 km", "10_km streams as \"10 km\"");
  os.str("");
  os << 1500_g << "|" << 3_kg << "|" << 90_s;
  check(os.str() == "1500 g|3 kg|90 s", "mass and time stream with their symbols");

  std::istringstream in("42 km");
  SI::kilo_metre_t<std::int64_t> distance{0};
  in >> distance;
  check(!in.fail() && distance.value() == 42, "\"42 km\" reads into kilo_metre_t");

  std::istringstream wrong("42 m");
  SI::kilo_metre_t<std::int64_t> rejected{0};
  wrong >> rejected;
  check(wrong.fail(), "a different unit symbol sets failbit");

  check(SI::to_string(5_kg) == "5 kg", "to_string");

  if (failures != 0) {
    return 1;
  }
  std::puts("si 2.5.3 C++ ok");
  return 0;
}
