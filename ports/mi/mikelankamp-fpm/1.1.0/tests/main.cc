#include <fpm/fixed.hpp>
#include <fpm/ios.hpp>
#include <fpm/math.hpp>

#include <cstdio>
#include <sstream>

namespace {

int failures = 0;

void check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "mikelankamp-fpm: %s\n", what);
    ++failures;
  }
}

}  // namespace

int main() {
  using F = fpm::fixed_16_16;

  // Q16.16 layout: raw value is the number scaled by 2^16.
  check(F(1).raw_value() == 65536, "raw_value of 1");
  check(F(1.5).raw_value() == 98304, "raw_value of 1.5");
  check(F::from_raw_value(1) == F(0.0000152587890625), "smallest step");

  check(F(1.5) + F(2.25) == F(3.75), "add");
  check(F(1.5) - F(2.25) == F(-0.75), "sub");
  check(F(1.5) * F(2) == F(3), "mul");
  check(F(-2.5) * F(0.5) == F(-1.25), "mul negative");
  check(F(3) / F(4) == F(0.75), "div");
  check(fpm::fmod(F(7), F(4)) == F(3), "fmod");
  check(-F(2) == F(-2), "negate");

  check(F(1) < F(2) && F(2) > F(1) && F(2) <= F(2) && F(2) >= F(2), "ordering");
  check(F(2) != F(3) && !(F(2) == F(3)), "equality");

  check(static_cast<int>(F(5.75)) == 5, "to int truncates");
  check(static_cast<int>(F(-5.75)) == -5, "to int truncates toward zero");
  check(static_cast<double>(F(5.75)) == 5.75, "to double");
  check(static_cast<double>(F(-0.375)) == -0.375, "to double negative");
  check(F(42) == F(42.0), "from int and double agree");

  check(fpm::sqrt(F(4)) == F(2), "sqrt 4");
  check(fpm::sqrt(F(2.25)) == F(1.5), "sqrt 2.25");
  check(fpm::floor(F(2.75)) == F(2), "floor");
  check(fpm::ceil(F(2.25)) == F(3), "ceil");
  check(fpm::abs(F(-2.5)) == F(2.5), "abs");

  {
    std::ostringstream os;
    os << F(2.5);
    check(os.str() == "2.5", "default stream output");
  }
  {
    std::ostringstream os;
    os.setf(std::ios::fixed, std::ios::floatfield);
    os.precision(3);
    os << F(2.5) << ' ' << F(-0.125);
    check(os.str() == "2.500 -0.125", "fixed precision output");
  }
  {
    std::istringstream is("3.25 -1.5");
    F a, b;
    is >> a >> b;
    check(!is.fail() && a == F(3.25) && b == F(-1.5), "stream input");
  }

  return failures == 0 ? 0 : 1;
}
