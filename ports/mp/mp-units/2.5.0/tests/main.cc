// API forms follow upstream's docs/users_guide/framework_basics (the_affine_space.md, text_output.md)
// and example/hello_units.cpp.  format.h and ostream.h are deprecated no-ops (#warning); formatting
// and streaming come with the quantity headers.
#include <mp-units/systems/isq.h>
#include <mp-units/systems/si.h>

#include <cstdio>
#include <format>
#include <sstream>
#include <string>

using namespace mp_units;
using namespace mp_units::si::unit_symbols;

// Dimension arithmetic: speed is length per time, and acceleration is speed per time.
static_assert(isq::speed.dimension == isq::length.dimension / isq::time.dimension);
static_assert(isq::acceleration.dimension == isq::length.dimension / (isq::time.dimension * isq::time.dimension));
static_assert(1 * km == 1000 * m);

namespace {

int failures = 0;

#define CHECK(cond)                                                    \
  do {                                                                 \
    if (!(cond)) {                                                     \
      std::fprintf(stderr, "main.cc:%d: check failed: %s\n", __LINE__, #cond); \
      ++failures;                                                      \
    }                                                                  \
  } while (0)

}  // namespace

int main() {
  // Arithmetic: speed from distance and time.
  quantity v = (100. * km) / (2. * h);
  CHECK(v.numerical_value_in(km / h) == 50.0);
  CHECK((10 * m / (2 * s)).numerical_value_in(m / s) == 5);

  // Unit conversion and truncating value_cast.
  CHECK((1 * km).numerical_value_in(m) == 1000);
  CHECK(value_cast<int>(2.7 * m).numerical_value_in(m) == 2);

  // quantity_point: a point plus a delta, and the difference of two points.
  quantity_point p1 = point<m>(100);
  quantity_point p2 = p1 + 20 * m;
  CHECK(p2.quantity_from_zero() == 120 * m);
  CHECK(p2.quantity_from(p1) == 20 * m);
  CHECK(p1.quantity_from(p2) == -20 * m);

  // Text output through std::format and operator<<.
  CHECK(std::format("{}", 10 * m) == "10 m");
  CHECK(std::format("{:%N in %U of %D}", 70 * km / h) == "70 in km/h of LT⁻¹");
  std::ostringstream os;
  os << 90 * km / h;
  CHECK(os.str() == "90 km/h");

  return failures == 0 ? 0 : 1;
}
