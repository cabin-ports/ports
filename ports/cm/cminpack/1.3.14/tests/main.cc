// The C++ consumer: cminpackcpp.hpp's overloads with the callables the C
// API cannot take (a capturing lambda, a stateful functor, a std::function,
// as upstream's examples/tcppwrap.cpp does), solving upstream's example
// problems to their reference outputs (examples/ref/tlmdif1c.ref,
// tlmderc.ref, tlmstr1c.ref, thybrj1c.ref); cminpack.h's C API with C
// linkage from C++; and a callback's negative return stopping the solver.
#include <cminpack.h>
#include <cminpackcpp.hpp>

#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <vector>

namespace {

bool ok = true;

void check(bool pass, const char* what)
{
  if (!pass) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ok = false;
  }
}

// Agreement with a reference printed to 7 significant digits.
bool near(double value, double reference)
{
  return std::fabs(value - reference) <= 1e-6 * std::fabs(reference) + 1e-12;
}

const std::vector<double> bard_y = { 1.4e-1, 1.8e-1, 2.2e-1, 2.5e-1, 2.9e-1, 3.2e-1, 3.5e-1, 3.9e-1,
                                     3.7e-1, 5.8e-1, 7.3e-1, 9.6e-1, 1.34,   2.1,    4.39 };

// Bard's fit from upstream's least-squares examples, against their output.
bool is_bard(int info, const double* x, const double* fvec)
{
  return info == 1 && near(::enorm(15, fvec), 0.09063596) && near(x[0], 0.08241058) &&
         near(x[1], 1.133037) && near(x[2], 2.343695);
}

// Row i of Bard's Jacobian.
void bard_row(const double* x, int i, double* row, int stride)
{
  double tmp1 = i + 1, tmp2 = 15 - i, tmp3 = i > 7 ? tmp2 : tmp1;
  double tmp4 = x[1] * tmp2 + x[2] * tmp3;
  tmp4 *= tmp4;
  row[0] = -1.;
  row[stride] = tmp1 * tmp2 / tmp4;
  row[2 * stride] = tmp1 * tmp3 / tmp4;
}

// A stateful functor for Broyden's tridiagonal system (thybrj1c.c) that
// counts its own calls.
struct Broyden {
  int calls = 0;
  int operator()(int n, const double* x, double* fvec, double* fjac, int ldfjac, int iflag)
  {
    ++calls;
    for (int k = 0; k < n; ++k) {
      if (iflag == 1) {
        fvec[k] = (3 - 2 * x[k]) * x[k] - (k > 0 ? x[k - 1] : 0) - 2 * (k < n - 1 ? x[k + 1] : 0) + 1;
        continue;
      }
      for (int j = 0; j < n; ++j)
        fjac[k + ldfjac * j] = 0.;
      fjac[k + ldfjac * k] = 3 - 4 * x[k];
      if (k > 0)
        fjac[k + ldfjac * (k - 1)] = -1.;
      if (k < n - 1)
        fjac[k + ldfjac * (k + 1)] = -2.;
    }
    return 0;
  }
};

}  // namespace

int main()
{
  const double tol = std::sqrt(::dpmpar(1));
  check(::dpmpar(1) == DBL_EPSILON, "dpmpar through C linkage");

  // A capturing lambda through lmdif1 (tlmdif1c.ref).
  std::vector<double> y = bard_y;
  int evaluations = 0;
  auto residuals = [&](int m, int, const double* x, double* fvec, int) {
    ++evaluations;
    for (int i = 0; i < m; ++i) {
      double tmp1 = i + 1, tmp2 = 15 - i, tmp3 = i > 7 ? tmp2 : tmp1;
      fvec[i] = y[i] - (x[0] + tmp1 / (x[1] * tmp2 + x[2] * tmp3));
    }
    return 0;
  };
  double x[3] = { 1., 1., 1. }, fvec[15], fjac[45], wa[75];
  int iwa[3], ipvt[3];
  int info = cminpack::lmdif1(residuals, 15, 3, x, fvec, tol, iwa, wa, 75);
  check(is_bard(info, x, fvec) && evaluations > 0, "lmdif1 with a capturing lambda");

  // The full lmder with a lambda (tlmderc.ref: 6 evaluations, 5 Jacobians).
  double diag[3], qtf[3], wa1[3], wa2[3], wa3[3], wa4[15];
  int nfev = 0, njev = 0;
  auto with_jacobian = [&](int m, int n, const double* x, double* fvec, double* fjac, int ldfjac, int iflag) {
    if (iflag == 1)
      return residuals(m, n, x, fvec, iflag);
    for (int i = 0; i < m; ++i)
      bard_row(x, i, fjac + i, ldfjac);
    return 0;
  };
  x[0] = x[1] = x[2] = 1.;
  info = cminpack::lmder(with_jacobian, 15, 3, x, fvec, fjac, 15, tol, tol, 0., 400, diag, 1, 100., 0, &nfev,
                         &njev, ipvt, qtf, wa1, wa2, wa3, wa4);
  check(is_bard(info, x, fvec) && nfev == 6 && njev == 5, "lmder with a lambda");

  // A std::function through lmstr1, one Jacobian row per call (tlmstr1c.ref).
  std::function<int(int, int, const double*, double*, double*, int)> rows =
    [&](int m, int n, const double* x, double* fvec, double* fjrow, int iflag) {
      if (iflag == 1)
        return residuals(m, n, x, fvec, iflag);
      bard_row(x, iflag - 2, fjrow, 1);
      return 0;
    };
  x[0] = x[1] = x[2] = 1.;
  info = cminpack::lmstr1(rows, 15, 3, x, fvec, fjac, 3, tol, ipvt, wa, 30);
  check(is_bard(info, x, fvec), "lmstr1 with a std::function");

  // A stateful functor through hybrj1 (thybrj1c.ref).
  static const double solution[9] = { -0.5706545, -0.6816283, -0.7017325, -0.7042129, -0.701369,
                                      -0.6918656, -0.665792,  -0.5960342, -0.4164121 };
  Broyden broyden;
  double z[9], fz[9], fjz[81], wz[99];
  for (double& v : z)
    v = -1.;
  info = cminpack::hybrj1(broyden, 9, z, fz, fjz, 9, tol, wz, 99);
  bool solved = info == 1 && ::enorm(9, fz) < 1e-7 && broyden.calls > 0;
  for (int i = 0; i < 9; ++i)
    solved = solved && near(z[i], solution[i]);
  check(solved, "hybrj1 with a stateful functor");

  // A negative return from the callable stops the solver and is passed on.
  x[0] = x[1] = x[2] = 1.;
  int stop_calls = 0;
  info = cminpack::lmdif1([&](int, int, const double*, double*, int) { return ++stop_calls < 3 ? 0 : -7; },
                          15, 3, x, fvec, tol, iwa, wa, 75);
  check(info == -7 && stop_calls == 3, "negative return stops lmdif1");

  if (!ok)
    return EXIT_FAILURE;
  std::puts("cminpack C++: ok");
  return EXIT_SUCCESS;
}
