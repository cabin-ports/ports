// gcem from C++11: functions evaluated in constant expressions
// (static_assert), the same functions at run time against <cmath>, special
// values, and return_t's promotion of integral arguments.
#include <cmath>
#include <cstdio>
#include <limits>
#include <type_traits>

#include <gcem.hpp>

namespace {

constexpr double kPi = 3.14159265358979323846;

// Upstream's tests accept |a - b| / (1 + |b|) below 1e-14 in long double,
// 1e-10 on Windows.  These checks run in double, where gcem's series leave a
// few 1e-14 (erf(1.9) is 4e-14 from <cmath>), so they accept 1e-12 on every
// platform.
constexpr double kTol = 1e-12;

constexpr bool agree(double a, double b) {
  return (a > b ? a - b : b - a) <= kTol * (1 + (b < 0 ? -b : b));
}

// Compile time: exact integer results, then reals against known values.
static_assert(gcem::factorial(10) == 3628800, "factorial");
static_assert(gcem::binomial_coef(10, 3) == 120, "binomial_coef");
static_assert(gcem::gcd(84, 36) == 12 && gcem::lcm(4, 6) == 12, "gcd, lcm");
static_assert(gcem::abs(-7) == 7, "abs");
static_assert(gcem::sgn(-3.5) == -1 && gcem::max(2, 9) == 9 &&
                  gcem::min(2, 9) == 2,
              "sgn, max, min");
static_assert(gcem::pow(2.0, 10) == 1024.0, "pow, integral exponent");
static_assert(gcem::floor(-2.5) == -3.0 && gcem::ceil(-2.5) == -2.0 &&
                  gcem::trunc(-2.7) == -2.0 && gcem::round(-4.5) == -5.0,
              "rounding");
static_assert(agree(gcem::sqrt(2.0), 1.4142135623730951), "sqrt");
static_assert(agree(gcem::exp(1.0), 2.718281828459045), "exp");
static_assert(agree(gcem::log(10.0), 2.302585092994046), "log");
static_assert(agree(gcem::sin(kPi / 6), 0.5), "sin");
static_assert(agree(gcem::cos(kPi / 3), 0.5), "cos");
static_assert(agree(gcem::atan(1.0), kPi / 4), "atan");
static_assert(agree(gcem::tgamma(5.0), 24.0), "tgamma");
static_assert(agree(gcem::erf(1.0), 0.8427007929497149), "erf");
static_assert(agree(gcem::pow(2.0, 0.5), 1.4142135623730951), "pow");
static_assert(gcem::signbit(-1.0) && !gcem::signbit(1.0), "signbit");
static_assert(gcem::copysign(3.0, -1.0) == -3.0, "copysign");
static_assert(std::is_same<decltype(gcem::sqrt(4)), double>::value,
              "integral arguments return double");
static_assert(std::is_same<decltype(gcem::sqrt(4.0f)), float>::value,
              "float arguments return float");
static_assert(std::is_same<decltype(gcem::factorial(10)), int>::value,
              "integral factorial stays integral");
#ifndef _MSC_VER
// MSVC's signbit() of a zero is not constexpr; these are checked at run
// time on every compiler below.
static_assert(gcem::signbit(-0.0) && !gcem::signbit(0.0), "signbit(+-0)");
static_assert(gcem::copysign(1.0, -0.0) == -1.0, "copysign(x, -0)");
#endif

int failures = 0;

void check(bool ok, int line, const char *what) {
  if (!ok) {
    std::fprintf(stderr, "main.cc:%d: check failed: %s\n", line, what);
    ++failures;
  }
}

#define CHECK(cond) check((cond), __LINE__, #cond)

void check_near(double actual, double expected, int line, const char *what) {
  if (!agree(actual, expected)) {
    std::fprintf(stderr, "main.cc:%d: check failed: %s: %.17g, expected %.17g\n",
                 line, what, actual, expected);
    ++failures;
  }
}

#define CHECK_NEAR(actual, expected) \
  check_near((actual), (expected), __LINE__, #actual)

// gcem::fn and std::fn agree on run-time arguments.
#define SAME(fn, ...) CHECK_NEAR(gcem::fn(__VA_ARGS__), std::fn(__VA_ARGS__))

}  // namespace

int main() {
  // volatile keeps the arguments, and so these calls, at run time.
  volatile double inputs[] = {-0.7, 0.3, 0.7, 1.9, 7.25};
  for (int i = 0; i < 5; ++i) {
    const double x = inputs[i];
    SAME(exp, x);
    SAME(expm1, x);
    SAME(sin, x);
    SAME(cos, x);
    SAME(tan, x);
    SAME(atan, x);
    SAME(sinh, x);
    SAME(cosh, x);
    SAME(tanh, x);
    SAME(asinh, x);
    SAME(erf, x);
    SAME(tgamma, x);
    SAME(floor, x);
    SAME(ceil, x);
    SAME(trunc, x);
    SAME(round, x);
    SAME(atan2, x, 2.0);
    SAME(atan2, -x, -2.0);
    SAME(hypot, x, 2.0);
    SAME(fmod, 10.0, x);
    // lgamma is only compared above zero: gcem's is +inf below DBL_MIN.
    if (x > 0) {
      SAME(sqrt, x);
      SAME(log, x);
      SAME(log1p, x);
      SAME(log2, x);
      SAME(log10, x);
      SAME(lgamma, x);
      SAME(pow, x, 2.5);
      CHECK_NEAR(gcem::inv_sqrt(x), 1 / std::sqrt(x));
      // The regularized lower incomplete gamma P(1, x) = 1 - e^-x.
      CHECK_NEAR(gcem::incomplete_gamma(1.0, x), -std::expm1(-x));
    }
    if (x > -1 && x < 1) {
      SAME(asin, x);
      SAME(acos, x);
      SAME(atanh, x);
      CHECK_NEAR(gcem::erf_inv(std::erf(x)), x);
    } else if (x >= 1) {
      SAME(acosh, x);
    }
  }

  // Below 1e-4 expm1 and log1p switch to series; compare them relatively
  // there.
  volatile double tiny = 3e-5;
  CHECK(std::fabs(gcem::expm1(tiny) / std::expm1(tiny) - 1) < kTol);
  CHECK(std::fabs(gcem::log1p(tiny) / std::log1p(tiny) - 1) < kTol);

  // Special functions with closed forms: I_z(2, 2) = 3z^2 - 2z^3,
  // I_z(3, 2) = 4z^3 - 3z^4, P(2, 1) = 1 - 2/e, and
  // B(a, b) = G(a) G(b) / G(a + b).
  volatile double z = 0.8, zero = 0.0, one = 1.0;
  CHECK_NEAR(gcem::incomplete_beta(2.0, 2.0, z), 3 * z * z - 2 * z * z * z);
  CHECK_NEAR(gcem::incomplete_beta(3.0, 2.0, z),
             4 * z * z * z - 3 * z * z * z * z);
  CHECK_NEAR(gcem::incomplete_gamma_inv(2.0, 1 - 2 / std::exp(1.0)), 1.0);
  CHECK_NEAR(gcem::beta(2.5, 1.5),
             std::tgamma(2.5) * std::tgamma(1.5) / std::tgamma(4.0));
  // A non-integral factorial is tgamma(x + 1).
  CHECK_NEAR(gcem::factorial(4.5 * one), std::tgamma(5.5));

  // Special values, as <cmath> gives them.
  const double inf = std::numeric_limits<double>::infinity();
  CHECK(std::isnan(gcem::sqrt(-one)));
  CHECK(std::isnan(gcem::log(-one)));
  CHECK(gcem::log(zero) == -inf);
  CHECK(gcem::exp(-inf) == 0.0);
  CHECK(gcem::exp(inf) == inf);
  CHECK(std::isnan(gcem::tgamma(-2 * one)));
  CHECK(gcem::signbit(-zero) && !gcem::signbit(zero));
  CHECK(gcem::copysign(one, -zero) == -1.0);
  // At the origin the signs of the zeros pick the quadrant.
  CHECK(gcem::atan2(-zero, -zero) == std::atan2(-zero, -zero));
  CHECK(gcem::atan2(zero, -zero) == std::atan2(zero, -zero));
  CHECK_NEAR(gcem::tgamma(0.5) * gcem::tgamma(0.5), kPi);

  if (failures != 0) {
    return 1;
  }
  std::printf("gcem %d.%d.%d: C++ API OK\n", GCEM_VERSION_MAJOR,
              GCEM_VERSION_MINOR, GCEM_VERSION_PATCH);
  return 0;
}
