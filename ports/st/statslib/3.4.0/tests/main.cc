#include <cmath>
#include <cstdio>

#include "stats.hpp"

static int failures = 0;

static void near(const char* what, double got, double want, double tol) {
  if (!(std::fabs(got - want) < tol)) {
    std::printf("FAIL %s: got %.10g, want %.10g\n", what, got, want);
    ++failures;
  }
}

// Compile-time evaluation through gcem; 1 / (2 * sqrt(2 * pi)) = 0.19947114.
constexpr double d_ct = stats::dnorm(1.0, 1.0, 2.0);
static_assert(d_ct > 0.199471 && d_ct < 0.199472, "dnorm is not a constant expression");

int main() {
  // Reference values are upstream's tests/ (upstream tolerance TEST_ERR_TOL 1e-6, stats_tests.hpp:44; absolute here).
  // dens/dnorm.cpp:32-38, mu = 1, sigma = 2.
  near("dnorm(2)", stats::dnorm(2.0, 1.0, 2.0), 0.17603266, 1e-6);
  near("dnorm(1)", stats::dnorm(1.0, 1.0, 2.0), 0.1994711, 1e-6);
  // dens/dexp.cpp:32-37, rate = 2.
  near("dexp(1)", stats::dexp(1.0, 2.0), 0.2706706, 1e-6);
  near("dexp(3)", stats::dexp(3.0, 2.0), 0.004957504, 1e-6);
  // prob/pnorm.cpp:32-38, mu = 1, sigma = 2.
  near("pnorm(-0.37)", stats::pnorm(-0.37, 1.0, 2.0), 0.246672, 1e-6);
  near("pnorm(2.5)", stats::pnorm(2.5, 1.0, 2.0), 0.7733726, 1e-6);
  // prob/pbinom.cpp:32-38, n = 4, p = 0.6 (exact: 0.5248, 0.8704).
  near("pbinom(2)", stats::pbinom(2, 4, 0.6), 0.5248, 1e-6);
  near("pbinom(3)", stats::pbinom(3, 4, 0.6), 0.8704, 1e-6);
  // quant/qnorm.cpp:34-40, mu = 1, sigma = 2.
  near("qnorm(0.15)", stats::qnorm(0.15, 1.0, 2.0), -1.072867, 1e-6);
  near("qnorm(0.79)", stats::qnorm(0.79, 1.0, 2.0), 2.612842, 1e-6);
  // quant/qchisq.cpp:35-40, dof = 6.
  near("qchisq(0.1)", stats::qchisq(0.1, 6.0), 2.204131, 1e-6);
  near("qchisq(0.95)", stats::qchisq(0.95, 6.0), 12.59159, 1e-5);

  // Random draws have no upstream reference values: check range only.
  stats::rand_engine_t engine(1776);
  for (int i = 0; i < 1000; ++i) {
    const double u = stats::runif(0.0, 1.0, engine);
    if (!(u >= 0.0 && u < 1.0)) {
      std::printf("FAIL runif draw %d: %.10g\n", i, u);
      ++failures;
    }
    const double g = stats::rgamma(2.0, 1.0, engine);
    if (!(g > 0.0)) {
      std::printf("FAIL rgamma draw %d: %.10g\n", i, g);
      ++failures;
    }
  }
  return failures == 0 ? 0 : 1;
}
