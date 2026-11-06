// Solves dense and sparse linear systems, detects a singular matrix,
// checks a symmetric eigendecomposition and a quaternion rotation, and
// evaluates a matrix exponential from an unsupported module.  C++14,
// the interface standard the port declares.
#include <Eigen/Dense>
#include <Eigen/Sparse>
#include <unsupported/Eigen/MatrixFunctions>

#include <cmath>
#include <cstdio>
#include <vector>

static_assert(EIGEN_VERSION_AT_LEAST(5, 0, 1), "Eigen 5.0.1 headers");

static bool fail(const char* what) {
    std::fprintf(stderr, "eigen test failed: %s\n", what);
    return false;
}

static bool near(double a, double b) { return std::abs(a - b) < 1e-12; }

static bool solves_dense_systems() {
    Eigen::Matrix3d a;
    a << 4, 1, 2,
         1, 3, 0,
         2, 0, 5;
    const Eigen::Vector3d expected(1, -2, 3);
    const Eigen::Vector3d b = a * expected;
    const Eigen::Vector3d lu = a.partialPivLu().solve(b);
    const Eigen::Vector3d llt = a.llt().solve(b);
    // Written as `!(error < tol)` so a NaN result fails too.
    if (!((lu - expected).norm() < 1e-12)) return fail("partialPivLu solve");
    if (!((llt - expected).norm() < 1e-12)) return fail("llt solve");

    Eigen::Matrix2d singular;
    singular << 1, 2,
                2, 4;
    const Eigen::FullPivLU<Eigen::Matrix2d> full(singular);
    return (full.rank() == 1 && !full.isInvertible()) || fail("singular matrix not detected");
}

static bool decomposes_and_rotates() {
    Eigen::Matrix2d s;
    s << 2, 1,
         1, 2;
    const Eigen::SelfAdjointEigenSolver<Eigen::Matrix2d> es(s);
    if (es.info() != Eigen::Success || !near(es.eigenvalues()(0), 1) ||
        !near(es.eigenvalues()(1), 3)) {
        return fail("eigenvalues");
    }
    const Eigen::Quaterniond q(Eigen::AngleAxisd(EIGEN_PI / 2, Eigen::Vector3d::UnitZ()));
    return (q * Eigen::Vector3d::UnitX() - Eigen::Vector3d::UnitY()).norm() < 1e-12 ||
           fail("quaternion rotation");
}

static bool solves_a_sparse_system() {
    const int n = 6;
    std::vector<Eigen::Triplet<double>> entries;
    for (int i = 0; i < n; ++i) {
        entries.emplace_back(i, i, 2.0);
        if (i + 1 < n) {
            entries.emplace_back(i, i + 1, -1.0);
            entries.emplace_back(i + 1, i, -1.0);
        }
    }
    Eigen::SparseMatrix<double> m(n, n);
    m.setFromTriplets(entries.begin(), entries.end());
    if (m.nonZeros() != 3 * n - 2) return fail("sparse assembly");
    const Eigen::VectorXd expected = Eigen::VectorXd::LinSpaced(n, 1, n);
    const Eigen::VectorXd b = m * expected;
    Eigen::SimplicialLDLT<Eigen::SparseMatrix<double>> ldlt(m);
    if (ldlt.info() != Eigen::Success) return fail("sparse factorization");
    return (ldlt.solve(b) - expected).norm() < 1e-10 || fail("sparse solve");
}

static bool exponentiates_a_matrix() {
    // exp of a nilpotent matrix is the finite series I + N.
    Eigen::Matrix2d n;
    n << 0, 1,
         0, 0;
    const Eigen::Matrix2d e = n.exp();
    return (near(e(0, 0), 1) && near(e(0, 1), 1) && near(e(1, 0), 0) && near(e(1, 1), 1)) ||
           fail("matrix exponential");
}

int main() {
    const bool ok = solves_dense_systems() && decomposes_and_rotates() &&
                    solves_a_sparse_system() && exponentiates_a_matrix();
    return ok ? 0 : 1;
}
