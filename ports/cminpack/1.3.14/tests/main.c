/* The C consumer, in C89: upstream's example problems (examples/tlmdif1c.c,
 * tlmderc.c, tlmstr1c.c, thybrd1c.c, thybrj1c.c, tchkderc.c, tlmder1_.c)
 * solved through the C API of cminpack.h and the Fortran-style API of
 * minpack.h, and held to upstream's reference outputs (examples/ref/), which
 * print 7 significant digits.  Bard's least-squares fit (15 residuals, 3
 * unknowns) through lmdif1, lmder with covar1's covariance, lmstr1 and
 * lmder1_; Broyden's tridiagonal system (9 equations) through hybrd1 and
 * hybrj1; chkder catching the Jacobian error tchkderc.c plants; and the
 * double-precision machine constants. */
#include <cminpack.h>
#include <minpack.h>

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static int failures;

static void check(int pass, const char *what)
{
  if (!pass) {
    fprintf(stderr, "FAIL: %s\n", what);
    failures++;
  }
}

/* Agreement with a reference printed to 7 significant digits. */
static int near(double value, double reference)
{
  return fabs(value - reference) <= 1e-6 * fabs(reference) + 1e-12;
}

static const double bard_y[15] = { 1.4e-1, 1.8e-1, 2.2e-1, 2.5e-1, 2.9e-1, 3.2e-1, 3.5e-1, 3.9e-1,
                                   3.7e-1, 5.8e-1, 7.3e-1, 9.6e-1, 1.34, 2.1, 4.39 };
static const double bard_x[3] = { 0.08241058, 1.133037, 2.343695 };
static const double bard_fnorm = 0.09063596;

static void bard_residuals(const double *y, const double *x, double *fvec)
{
  int i;
  for (i = 0; i < 15; ++i) {
    double tmp1 = i + 1, tmp2 = 15 - i, tmp3 = i > 7 ? tmp2 : tmp1;
    fvec[i] = y[i] - (x[0] + tmp1 / (x[1] * tmp2 + x[2] * tmp3));
  }
}

/* Row i of the Jacobian; tchkderc.c's planted error uses tmp2 for tmp3. */
static void bard_row(const double *x, int i, int planted, double *row, int stride)
{
  double tmp1 = i + 1, tmp2 = 15 - i, tmp3 = i > 7 || planted ? tmp2 : tmp1;
  double tmp4 = x[1] * tmp2 + x[2] * tmp3;
  tmp4 *= tmp4;
  row[0] = -1.;
  row[stride] = tmp1 * tmp2 / tmp4;
  row[2 * stride] = tmp1 * tmp3 / tmp4;
}

static int bard_fcn(void *p, int m, int n, const double *x, double *fvec, int iflag)
{
  (void)m;
  (void)n;
  (void)iflag;
  bard_residuals((const double *)p, x, fvec);
  return 0;
}

static int bard_fcnder(void *p, int m, int n, const double *x, double *fvec, double *fjac, int ldfjac,
                       int iflag)
{
  int i;
  (void)m;
  (void)n;
  if (iflag == 1)
    bard_residuals((const double *)p, x, fvec);
  else if (iflag == 2)
    for (i = 0; i < 15; ++i)
      bard_row(x, i, 0, fjac + i, ldfjac);
  return 0;
}

/* lmstr1 asks for one Jacobian row at a time, row iflag - 2. */
static int bard_fcnderstr(void *p, int m, int n, const double *x, double *fvec, double *fjrow, int iflag)
{
  (void)m;
  (void)n;
  if (iflag == 1)
    bard_residuals((const double *)p, x, fvec);
  else if (iflag >= 2)
    bard_row(x, iflag - 2, 0, fjrow, 1);
  return 0;
}

/* minpack.h's Fortran convention: every argument by pointer, no void *. */
static void bard_fortran(const int *m, const int *n, const double *x, double *fvec, double *fjac,
                         const int *ldfjac, int *iflag)
{
  int i;
  (void)m;
  (void)n;
  if (*iflag == 1)
    bard_residuals(bard_y, x, fvec);
  else
    for (i = 0; i < 15; ++i)
      bard_row(x, i, 0, fjac + i, *ldfjac);
}

static void check_bard(int info, const double *x, const double *fvec, const char *what)
{
  check(info == 1, what);
  check(near(__cminpack_func__(enorm)(15, fvec), bard_fnorm), what);
  check(near(x[0], bard_x[0]) && near(x[1], bard_x[1]) && near(x[2], bard_x[2]), what);
}

static void test_bard(void)
{
  static const double covariance[9] = { 0.0001531202, 0.002869941,  -0.002656662,
                                        0.002869941,  0.09480935,   -0.09098995,
                                        -0.002656662, -0.09098995,  0.08778727 };
  double x[3], fvec[15], fjac[45], diag[3], qtf[3], wa[75], wa1[3], wa2[3], wa3[3], wa4[15];
  double tol = sqrt(__cminpack_func__(dpmpar)(1)), fnorm;
  int iwa[3], ipvt[3], info, nfev, njev, i, m = 15, n = 3, ldfjac = 15, lwa = 75;

  /* examples/tlmdif1c.c, ref/tlmdif1c.ref */
  x[0] = x[1] = x[2] = 1.;
  info = __cminpack_func__(lmdif1)(bard_fcn, (void *)bard_y, 15, 3, x, fvec, tol, iwa, wa, 75);
  check_bard(info, x, fvec, "lmdif1");

  /* examples/tlmderc.c, ref/tlmderc.ref: 6 evaluations, 5 Jacobians */
  x[0] = x[1] = x[2] = 1.;
  info = __cminpack_func__(lmder)(bard_fcnder, (void *)bard_y, 15, 3, x, fvec, fjac, 15, tol, tol, 0., 400,
                                  diag, 1, 100., 0, &nfev, &njev, ipvt, qtf, wa1, wa2, wa3, wa4);
  check_bard(info, x, fvec, "lmder");
  check(nfev == 6 && njev == 5, "lmder evaluations");
  fnorm = __cminpack_func__(enorm)(15, fvec);
  check(__cminpack_func__(covar1)(15, 3, fnorm * fnorm, fjac, 15, ipvt, __cminpack_func__(dpmpar)(1),
                                  wa1) == 0,
        "covar1 full rank");
  for (i = 0; i < 9; ++i)
    check(near(fjac[(i / 3) * 15 + i % 3], covariance[i]), "covar1 covariance");

  /* examples/tlmstr1c.c, ref/tlmstr1c.ref */
  x[0] = x[1] = x[2] = 1.;
  info = __cminpack_func__(lmstr1)(bard_fcnderstr, (void *)bard_y, 15, 3, x, fvec, fjac, 3, tol, ipvt, wa,
                                   30);
  check_bard(info, x, fvec, "lmstr1");

  /* examples/tlmder1_.c, ref/tlmder1c.ref, through minpack.h */
  x[0] = x[1] = x[2] = 1.;
  info = 0;
  __minpack_func__(lmder1)(bard_fortran, &m, &n, x, fvec, fjac, &ldfjac, &tol, &info, ipvt, wa, &lwa);
  check_bard(info, x, fvec, "lmder1_");
  check(__minpack_func__(enorm)(&m, fvec) == __cminpack_func__(enorm)(15, fvec), "enorm_");
}

/* examples/tchkderc.c, ref/tchkderc.ref: the first seven gradients carry the
 * planted error, which vanishes in the eighth (there tmp1 == tmp2), and the
 * last seven are right. */
static void test_chkder(void)
{
  static const double err_ref[7] = { 0.1141397, 0.09943516, 0.09674474, 0.09980447,
                                     0.1073116, 0.1220445,  0.1526814 };
  double x[3], xp[3], fvec[15], fvecp[15], fjac[45], err[15];
  int i;

  x[0] = 9.2e-1;
  x[1] = 1.3e-1;
  x[2] = 5.4e-1;
  __cminpack_func__(chkder)(15, 3, x, NULL, NULL, 15, xp, NULL, 1, NULL);
  bard_residuals(bard_y, x, fvec);
  for (i = 0; i < 15; ++i)
    bard_row(x, i, 1, fjac + i, 15);
  bard_residuals(bard_y, xp, fvecp);
  __cminpack_func__(chkder)(15, 3, x, fvec, fjac, 15, NULL, fvecp, 2, err);
  for (i = 0; i < 7; ++i)
    check(fabs(err[i] - err_ref[i]) < 1e-4, "chkder flags the planted error");
  check(err[7] > 0.99, "chkder at the row where the planted error vanishes");
  for (i = 8; i < 15; ++i)
    check(err[i] > 0.99, "chkder accepts the right gradients");
}

/* Broyden's tridiagonal system: examples/thybrd1c.c and thybrj1c.c. */
static int broyden_fcn(void *p, int n, const double *x, double *fvec, int iflag)
{
  int k;
  (void)p;
  (void)iflag;
  for (k = 0; k < n; ++k)
    fvec[k] = (3 - 2 * x[k]) * x[k] - (k > 0 ? x[k - 1] : 0) - 2 * (k < n - 1 ? x[k + 1] : 0) + 1;
  return 0;
}

static int broyden_fcnder(void *p, int n, const double *x, double *fvec, double *fjac, int ldfjac, int iflag)
{
  int j, k;
  if (iflag == 1)
    return broyden_fcn(p, n, x, fvec, iflag);
  for (k = 0; k < n; ++k) {
    for (j = 0; j < n; ++j)
      fjac[k + ldfjac * j] = 0.;
    fjac[k + ldfjac * k] = 3 - 4 * x[k];
    if (k > 0)
      fjac[k + ldfjac * (k - 1)] = -1.;
    if (k < n - 1)
      fjac[k + ldfjac * (k + 1)] = -2.;
  }
  return 0;
}

static void test_broyden(void)
{
  static const double solution[9] = { -0.5706545, -0.6816283, -0.7017325, -0.7042129, -0.701369,
                                      -0.6918656, -0.665792,  -0.5960342, -0.4164121 };
  double x[9], fvec[9], fjac[81], wa[180], tol = sqrt(__cminpack_func__(dpmpar)(1));
  int i, info, pass;

  for (i = 0; i < 9; ++i)
    x[i] = -1.;
  info = __cminpack_func__(hybrd1)(broyden_fcn, NULL, 9, x, fvec, tol, wa, 180);
  pass = info == 1 && __cminpack_func__(enorm)(9, fvec) < 1e-7;
  for (i = 0; i < 9; ++i)
    pass = pass && near(x[i], solution[i]);
  check(pass, "hybrd1");

  for (i = 0; i < 9; ++i)
    x[i] = -1.;
  info = __cminpack_func__(hybrj1)(broyden_fcnder, NULL, 9, x, fvec, fjac, 9, tol, wa, 99);
  pass = info == 1 && __cminpack_func__(enorm)(9, fvec) < 1e-7;
  for (i = 0; i < 9; ++i)
    pass = pass && near(x[i], solution[i]);
  check(pass, "hybrj1");
}

int main(void)
{
  check(__cminpack_func__(dpmpar)(1) == DBL_EPSILON && __cminpack_func__(dpmpar)(2) == DBL_MIN &&
            __cminpack_func__(dpmpar)(3) == DBL_MAX,
        "double-precision dpmpar");
  test_bard();
  test_chkder();
  test_broyden();
  if (failures)
    return EXIT_FAILURE;
  puts("cminpack: ok");
  return EXIT_SUCCESS;
}
