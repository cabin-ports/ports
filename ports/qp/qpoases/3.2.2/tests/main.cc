#include <qpOASES.hpp>

#include <cmath>
#include <cstdio>

static int failures = 0;

static void expect(const char *what, double got, double want) {
  if (!(std::fabs(got - want) <= 1e-9)) {
    std::printf("FAIL %s: got %.12g, want %.12g\n", what, got, want);
    ++failures;
  }
}

int main() {
  using namespace qpOASES;

  Options options;
  options.printLevel = PL_NONE;

  // upstream's examples/example1.cpp: two QPs solved with init and hotstart.
  real_t H[2 * 2] = {1.0, 0.0, 0.0, 0.5};
  real_t A[1 * 2] = {1.0, 1.0};
  real_t g[2] = {1.5, 1.0};
  real_t lb[2] = {0.5, -2.0};
  real_t ub[2] = {5.0, 2.0};
  real_t lbA[1] = {-1.0};
  real_t ubA[1] = {2.0};
  real_t g2[2] = {1.0, 1.5};
  real_t lb2[2] = {0.0, -1.0};
  real_t ub2[2] = {5.0, -0.5};
  real_t lbA2[1] = {-2.0};
  real_t ubA2[1] = {1.0};

  QProblem qp(2, 1);
  qp.setOptions(options);
  int_t nWSR = 10;
  if (qp.init(H, g, A, lb, ub, lbA, ubA, nWSR) != SUCCESSFUL_RETURN) {
    std::printf("FAIL QProblem::init\n");
    return 1;
  }
  real_t x[2], y[3];
  qp.getPrimalSolution(x);
  qp.getDualSolution(y);
  expect("qp1 x0", x[0], 0.5);
  expect("qp1 x1", x[1], -1.5);
  expect("qp1 y0", y[0], 1.75);
  expect("qp1 y1", y[1], 0.0);
  expect("qp1 y2", y[2], 0.25);
  expect("qp1 obj", qp.getObjVal(), -0.0625);

  nWSR = 10;
  if (qp.hotstart(g2, lb2, ub2, lbA2, ubA2, nWSR) != SUCCESSFUL_RETURN) {
    std::printf("FAIL QProblem::hotstart\n");
    return 1;
  }
  qp.getPrimalSolution(x);
  qp.getDualSolution(y);
  expect("qp2 x0", x[0], 0.0);
  expect("qp2 x1", x[1], -1.0);
  expect("qp2 y0", y[0], 1.0);
  expect("qp2 y1", y[1], 1.0);
  expect("qp2 y2", y[2], 0.0);
  expect("qp2 obj", qp.getObjVal(), -1.25);

  // Bound-constrained QP: the same H and g with x1 >= -1 active.
  real_t bl[2] = {0.5, -1.0};
  real_t bu[2] = {5.0, 2.0};
  QProblemB qpb(2);
  qpb.setOptions(options);
  nWSR = 10;
  if (qpb.init(H, g, bl, bu, nWSR) != SUCCESSFUL_RETURN) {
    std::printf("FAIL QProblemB::init\n");
    return 1;
  }
  qpb.getPrimalSolution(x);
  qpb.getDualSolution(y);
  expect("qpb x0", x[0], 0.5);
  expect("qpb x1", x[1], -1.0);
  expect("qpb y0", y[0], 2.0);
  expect("qpb y1", y[1], 0.5);
  expect("qpb obj", qpb.getObjVal(), 0.125);

  // Reads the per-OS clock define, else -1.
  if (getCPUtime() <= 0.0) {
    std::printf("FAIL getCPUtime\n");
    ++failures;
  }
  return failures == 0 ? 0 : 1;
}
