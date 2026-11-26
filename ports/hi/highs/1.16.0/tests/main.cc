// The C++ consumer: solves upstream's example LP
// (examples/call_highs_from_cpp.cpp), a MIP whose optimum differs from its
// LP relaxation's, and the same MIP read from a gzip-compressed LP file.
#include "Highs.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

bool approx(double a, double b) { return std::fabs(a - b) <= 1e-6; }

bool solved(Highs& highs) {
  return highs.run() == HighsStatus::kOk &&
         highs.getModelStatus() == HighsModelStatus::kOptimal;
}

// min x0 + x1 + 3 s.t. x1 <= 7, 5 <= x0 + 2 x1 <= 15, 6 <= 3 x0 + 2 x1,
// 0 <= x0 <= 4, 1 <= x1: optimal at (0.5, 2.25), objective 5.75.
void solves_upstream_example_lp() {
  HighsLp lp;
  lp.num_col_ = 2;
  lp.num_row_ = 3;
  lp.sense_ = ObjSense::kMinimize;
  lp.offset_ = 3;
  lp.col_cost_ = {1.0, 1.0};
  lp.col_lower_ = {0.0, 1.0};
  lp.col_upper_ = {4.0, kHighsInf};
  lp.row_lower_ = {-kHighsInf, 5.0, 6.0};
  lp.row_upper_ = {7.0, 15.0, kHighsInf};
  lp.a_matrix_.format_ = MatrixFormat::kColwise;
  lp.a_matrix_.start_ = {0, 2, 5};
  lp.a_matrix_.index_ = {1, 2, 0, 1, 2};
  lp.a_matrix_.value_ = {1.0, 3.0, 1.0, 2.0, 2.0};

  Highs highs;
  highs.setOptionValue("output_flag", false);
  check(highs.passModel(lp) == HighsStatus::kOk, "LP passModel");
  check(solved(highs), "LP solved to optimality");
  check(approx(highs.getInfo().objective_function_value, 5.75), "LP objective 5.75");
  const HighsSolution& s = highs.getSolution();
  check(s.col_value.size() == 2 && approx(s.col_value[0], 0.5) &&
            approx(s.col_value[1], 2.25),
        "LP solution (0.5, 2.25)");
  check(s.row_value.size() == 3 && approx(s.row_value[0], 2.25) &&
            approx(s.row_value[1], 5.0) && approx(s.row_value[2], 6.0),
        "LP row activities (2.25, 5, 6)");
}

// max 5 x + 8 y s.t. x + y <= 6, 5 x + 9 y <= 45, x, y >= 0 integer: the
// only optimum is (0, 5) with 40; the relaxation's is (2.25, 3.75), 41.25.
HighsLp knapsack_mip() {
  HighsLp lp;
  lp.num_col_ = 2;
  lp.num_row_ = 2;
  lp.sense_ = ObjSense::kMaximize;
  lp.col_cost_ = {5.0, 8.0};
  lp.col_lower_ = {0.0, 0.0};
  lp.col_upper_ = {kHighsInf, kHighsInf};
  lp.row_lower_ = {-kHighsInf, -kHighsInf};
  lp.row_upper_ = {6.0, 45.0};
  lp.a_matrix_.format_ = MatrixFormat::kRowwise;
  lp.a_matrix_.start_ = {0, 2, 4};
  lp.a_matrix_.index_ = {0, 1, 0, 1};
  lp.a_matrix_.value_ = {1.0, 1.0, 5.0, 9.0};
  lp.integrality_ = {HighsVarType::kInteger, HighsVarType::kInteger};
  return lp;
}

void check_mip_optimum(Highs& highs, const char* what) {
  check(solved(highs), what);
  check(approx(highs.getInfo().objective_function_value, 40.0), what);
  const HighsSolution& s = highs.getSolution();
  check(s.col_value.size() == 2 && approx(s.col_value[0], 0.0) &&
            approx(s.col_value[1], 5.0),
        what);
}

void solves_mip_and_its_relaxation() {
  Highs highs;
  highs.setOptionValue("output_flag", false);
  check(highs.passModel(knapsack_mip()) == HighsStatus::kOk, "MIP passModel");
  check_mip_optimum(highs, "MIP optimum (0, 5), objective 40");

  const HighsVarType continuous[] = {HighsVarType::kContinuous,
                                     HighsVarType::kContinuous};
  check(highs.changeColsIntegrality(0, 1, continuous) == HighsStatus::kOk,
        "relax integrality");
  check(solved(highs), "relaxation solved to optimality");
  check(approx(highs.getInfo().objective_function_value, 41.25),
        "relaxation objective 41.25");
  const HighsSolution& s = highs.getSolution();
  check(s.col_value.size() == 2 && approx(s.col_value[0], 2.25) &&
            approx(s.col_value[1], 3.75),
        "relaxation solution (2.25, 3.75)");
}

// gzip -9n of the CPLEX LP file
//   Maximize\n obj: 5 x + 8 y\nSubject To\n c1: x + y <= 6\n
//   c2: 5 x + 9 y <= 45\nGeneral\n x y\nEnd\n
// (the MIP above), which HiGHS reads through zlib.
const unsigned char knapsack_lp_gz[] = {
    0x1f, 0x8b, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0xff, 0xf3, 0x4d,
    0xac, 0xc8, 0xcc, 0xcd, 0xac, 0x4a, 0xe5, 0x52, 0xc8, 0x4f, 0xca, 0xb2,
    0x52, 0x30, 0x55, 0xa8, 0x50, 0xd0, 0x56, 0xb0, 0x50, 0xa8, 0xe4, 0x0a,
    0x2e, 0x4d, 0xca, 0x4a, 0x4d, 0x2e, 0x51, 0x08, 0xc9, 0xe7, 0x52, 0x48,
    0x36, 0xb4, 0x02, 0x8b, 0x57, 0x2a, 0xd8, 0xd8, 0x2a, 0x98, 0x01, 0xf9,
    0x46, 0x30, 0x95, 0x96, 0x10, 0x31, 0x13, 0x53, 0x2e, 0xf7, 0xd4, 0xbc,
    0xd4, 0xa2, 0xc4, 0x1c, 0x2e, 0xa0, 0x70, 0x25, 0x97, 0x6b, 0x5e, 0x0a,
    0x17, 0x00, 0xfc, 0xea, 0x68, 0x17, 0x5a, 0x00, 0x00, 0x00};

void reads_a_gzip_compressed_model() {
  // Among the probe's own build outputs: `cabin run` keeps the caller's
  // working directory.
  const char* const dir = std::getenv("CABIN_PROFILE_DIR");
  check(dir != nullptr, "CABIN_PROFILE_DIR is set");
  if (dir == nullptr) return;
  const std::string path = std::string(dir) + "/port-probe-highs.lp.gz";
  std::FILE* file = std::fopen(path.c_str(), "wb");
  check(file != nullptr, "create the .lp.gz file");
  if (file == nullptr) return;
  const bool written = std::fwrite(knapsack_lp_gz, 1, sizeof knapsack_lp_gz,
                                   file) == sizeof knapsack_lp_gz;
  check(std::fclose(file) == 0 && written, "write the .lp.gz file");

  Highs highs;
  highs.setOptionValue("output_flag", false);
  check(highs.readModel(path) == HighsStatus::kOk, "readModel of .lp.gz");
  const HighsLp& lp = highs.getLp();
  check(lp.num_col_ == 2 && lp.num_row_ == 2 &&
            lp.integrality_ ==
                std::vector<HighsVarType>(2, HighsVarType::kInteger),
        "the .lp.gz model has 2 integer columns and 2 rows");
  check_mip_optimum(highs, ".lp.gz MIP optimum (0, 5), objective 40");
  std::remove(path.c_str());
}

}  // namespace

int main() {
  check(highsVersionMajor() == 1 && highsVersionMinor() == 16 &&
            highsVersionPatch() == 0,
        "version 1.16.0");
  check(std::string(highsGithash()) == "n/a", "githash n/a");
  solves_upstream_example_lp();
  solves_mip_and_its_relaxation();
  reads_a_gzip_compressed_model();
  if (failures != 0) return EXIT_FAILURE;
  std::puts("highs C++ consumer: OK");
  return EXIT_SUCCESS;
}
