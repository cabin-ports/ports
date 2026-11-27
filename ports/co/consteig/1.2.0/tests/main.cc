// Matrix multiply, transpose, determinant and eigenvalues in constant
// expressions (static_assert), then the same operations at run time.
#include <consteig/consteig.hpp>

#include <cstdio>

namespace {

using consteig::Matrix;

constexpr double kTol = 1e-9;

constexpr double absd(double x) { return x < 0 ? -x : x; }

constexpr Matrix<int, 2, 3> kA{{{1, 2, 3}, {4, 5, 6}}};
constexpr Matrix<int, 3, 2> kAt = consteig::transpose(kA);
constexpr Matrix<int, 2, 2> kAAt = kA * kAt;

static_assert(kAt(0, 1) == 4 && kAt(2, 0) == 3 && kAt(2, 1) == 6, "transpose");
static_assert(kAAt(0, 0) == 14 && kAAt(0, 1) == 32 && kAAt(1, 0) == 32 &&
                  kAAt(1, 1) == 77,
              "multiply");
static_assert(consteig::determinant(kAAt) == 54, "determinant of A*A^T");
static_assert(kAAt.isSymmetric(), "A*A^T is symmetric");

// Symmetric tridiagonal matrix with eigenvalues 2-sqrt(2), 2, 2+sqrt(2) and
// determinant 4.
constexpr Matrix<double, 3, 3> kS{
    {{2.0, -1.0, 0.0}, {-1.0, 2.0, -1.0}, {0.0, -1.0, 2.0}}};
constexpr double kSqrt2 = 1.41421356237309504880;
constexpr auto kEigs = consteig::eigenvalues(kS);

static_assert(absd(consteig::determinant(kS) - 4.0) < kTol, "determinant");

// The eigenvalue order is unspecified: each expected value must be matched
// by exactly one computed eigenvalue, with no imaginary part.
constexpr bool has_eigenvalue(double want) {
  int hits = 0;
  for (consteig::Size i = 0; i < 3; ++i)
    if (absd(kEigs(i, 0).real - want) < kTol && absd(kEigs(i, 0).imag) < kTol)
      ++hits;
  return hits == 1;
}

static_assert(has_eigenvalue(2.0 - kSqrt2) && has_eigenvalue(2.0) &&
                  has_eigenvalue(2.0 + kSqrt2),
              "eigenvalues");

}  // namespace

int main() {
  int failures = 0;
  auto check = [&](bool ok, const char* what) {
    if (!ok) {
      std::fprintf(stderr, "FAIL: %s\n", what);
      ++failures;
    }
  };

  // Run-time values built from a volatile input, so nothing is folded away.
  volatile double v = 3.0;
  const Matrix<double, 2, 2> m{{{v, 1.0}, {1.0, v}}};
  const auto mm = m * consteig::transpose(m);
  check(absd(mm(0, 0) - 10.0) < kTol && absd(mm(0, 1) - 6.0) < kTol &&
            absd(mm(1, 1) - 10.0) < kTol,
        "run-time multiply");
  check(absd(consteig::determinant(m) - 8.0) < kTol, "run-time determinant");

  // Eigenvalues of [[3, 1], [1, 3]] are 2 and 4.
  const auto e = consteig::eigenvalues(m);
  const bool order = absd(e(0, 0).real - 2.0) < kTol;
  check(absd(e(order ? 0 : 1, 0).real - 2.0) < kTol &&
            absd(e(order ? 1 : 0, 0).real - 4.0) < kTol &&
            absd(e(0, 0).imag) < kTol && absd(e(1, 0).imag) < kTol,
        "run-time eigenvalues");

  // A rotation matrix has the complex pair +-i.
  const Matrix<double, 2, 2> rot{{{0.0, -1.0}, {1.0, 0.0}}};
  const auto r = consteig::eigenvalues(rot);
  check(absd(r(0, 0).real) < kTol && absd(r(1, 0).real) < kTol &&
            absd(absd(r(0, 0).imag) - 1.0) < kTol &&
            absd(r(0, 0).imag + r(1, 0).imag) < kTol,
        "complex eigenvalues");

  return failures == 0 ? 0 : 1;
}
