/* The C consumer, through interfaces/highs_c_api.h: upstream's example LP
 * (examples/call_highs_from_c_minimal.c) minimized and maximized with
 * Highs_lpCall, and a MIP through a Highs object. */
#include "interfaces/highs_c_api.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static int failures = 0;

static void check(int ok, const char* what) {
  if (!ok) {
    fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

static int approx(double a, double b) { return fabs(a - b) <= 1e-6; }

/* min/max x0 + x1 + 3 s.t. x1 <= 7, 5 <= x0 + 2 x1 <= 15, 6 <= 3 x0 + 2 x1,
 * 0 <= x0 <= 4, 1 <= x1: the minimum is at (0.5, 2.25), the maximum at
 * (4, 5.5). */
static void solves_upstream_example_lp(void) {
  const double col_cost[2] = {1.0, 1.0};
  const double col_lower[2] = {0.0, 1.0};
  const double col_upper[2] = {4.0, 1.0e30};
  const double row_lower[3] = {-1.0e30, 5.0, 6.0};
  const double row_upper[3] = {7.0, 15.0, 1.0e30};
  const HighsInt a_start[2] = {0, 2};
  const HighsInt a_index[5] = {1, 2, 0, 1, 2};
  const double a_value[5] = {1.0, 3.0, 1.0, 2.0, 2.0};
  double col_value[2], col_dual[2], row_value[3], row_dual[3];
  HighsInt col_basis[2], row_basis[3], model_status = -1;

  HighsInt status = Highs_lpCall(
      2, 3, 5, kHighsMatrixFormatColwise, kHighsObjSenseMinimize, 3.0,
      col_cost, col_lower, col_upper, row_lower, row_upper, a_start, a_index,
      a_value, col_value, col_dual, row_value, row_dual, col_basis, row_basis,
      &model_status);
  check(status == kHighsStatusOk, "Highs_lpCall minimize status");
  check(model_status == kHighsModelStatusOptimal, "minimum is optimal");
  check(approx(col_value[0], 0.5) && approx(col_value[1], 2.25),
        "minimum at (0.5, 2.25)");
  check(approx(row_value[0], 2.25) && approx(row_value[1], 5.0) &&
            approx(row_value[2], 6.0),
        "row activities (2.25, 5, 6)");

  model_status = -1;
  status = Highs_lpCall(
      2, 3, 5, kHighsMatrixFormatColwise, kHighsObjSenseMaximize, 3.0,
      col_cost, col_lower, col_upper, row_lower, row_upper, a_start, a_index,
      a_value, col_value, col_dual, row_value, row_dual, col_basis, row_basis,
      &model_status);
  check(status == kHighsStatusOk, "Highs_lpCall maximize status");
  check(model_status == kHighsModelStatusOptimal, "maximum is optimal");
  check(approx(col_value[0], 4.0) && approx(col_value[1], 5.5),
        "maximum at (4, 5.5)");
}

/* max 5 x + 8 y s.t. x + y <= 6, 5 x + 9 y <= 45, x, y >= 0 integer: the
 * only optimum is (0, 5) with 40; the relaxation's is 41.25. */
static void solves_a_mip(void) {
  const double col_cost[2] = {5.0, 8.0};
  const double col_lower[2] = {0.0, 0.0};
  const double col_upper[2] = {1.0e30, 1.0e30};
  const double row_lower[2] = {-1.0e30, -1.0e30};
  const double row_upper[2] = {6.0, 45.0};
  const HighsInt a_start[2] = {0, 2};
  const HighsInt a_index[4] = {0, 1, 0, 1};
  const double a_value[4] = {1.0, 1.0, 5.0, 9.0};
  const HighsInt integrality[2] = {kHighsVarTypeInteger, kHighsVarTypeInteger};
  double col_value[2], col_dual[2], row_value[2], row_dual[2];

  void* highs = Highs_create();
  check(highs != NULL, "Highs_create");
  if (highs == NULL) return;
  check(Highs_setBoolOptionValue(highs, "output_flag", 0) == kHighsStatusOk,
        "silence the log");
  check(Highs_passMip(highs, 2, 2, 4, kHighsMatrixFormatRowwise,
                      kHighsObjSenseMaximize, 0.0, col_cost, col_lower,
                      col_upper, row_lower, row_upper, a_start, a_index,
                      a_value, integrality) == kHighsStatusOk,
        "Highs_passMip");
  check(Highs_run(highs) == kHighsStatusOk, "Highs_run");
  check(Highs_getModelStatus(highs) == kHighsModelStatusOptimal,
        "MIP is optimal");
  check(approx(Highs_getObjectiveValue(highs), 40.0), "MIP objective 40");
  check(Highs_getSolution(highs, col_value, col_dual, row_value, row_dual) ==
            kHighsStatusOk,
        "Highs_getSolution");
  check(approx(col_value[0], 0.0) && approx(col_value[1], 5.0),
        "MIP optimum (0, 5)");
  Highs_destroy(highs);
}

int main(void) {
  check(Highs_versionMajor() == 1 && Highs_versionMinor() == 16 &&
            Highs_versionPatch() == 0,
        "version 1.16.0");
  solves_upstream_example_lp();
  solves_a_mip();
  if (failures != 0) return EXIT_FAILURE;
  puts("highs C consumer: OK");
  return EXIT_SUCCESS;
}
