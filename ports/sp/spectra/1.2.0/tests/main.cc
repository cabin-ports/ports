// Computes eigenvalues of a dense symmetric, a sparse symmetric and a
// dense non-symmetric matrix whose spectra are known in closed form.
#include <Eigen/Core>
#include <Eigen/SparseCore>
#include <Spectra/GenEigsSolver.h>
#include <Spectra/MatOp/DenseGenMatProd.h>
#include <Spectra/MatOp/DenseSymMatProd.h>
#include <Spectra/MatOp/SparseSymMatProd.h>
#include <Spectra/SymEigsSolver.h>

#include <cmath>
#include <complex>
#include <cstdio>
#include <vector>

using namespace Spectra;

static bool fail(const char* what) {
    std::fprintf(stderr, "spectra test failed: %s\n", what);
    return false;
}

// A NaN result compares false, so it is never close.
static bool close(double a, double b, double tol) { return std::abs(a - b) < tol; }

static bool dense_symmetric() {
    const int n = 10;
    Eigen::MatrixXd m = Eigen::MatrixXd::Zero(n, n);
    for (int i = 0; i < n; i++) m(i, i) = i + 1;
    DenseSymMatProd<double> op(m);
    SymEigsSolver<DenseSymMatProd<double>> eigs(op, 3, 6);
    eigs.init();
    if (eigs.compute(SortRule::LargestAlge) != 3 || eigs.info() != CompInfo::Successful)
        return fail("dense symmetric status");
    const Eigen::VectorXd ev = eigs.eigenvalues();
    const Eigen::MatrixXd vec = eigs.eigenvectors();
    for (int k = 0; k < 3; k++) {
        if (!close(ev(k), 10 - k, 1e-10)) return fail("dense symmetric eigenvalue");
        if (!close(std::abs(vec(9 - k, k)), 1, 1e-8)) return fail("dense symmetric eigenvector");
    }
    return true;
}

static bool sparse_symmetric() {
    const int n = 10;
    std::vector<Eigen::Triplet<double>> t;
    for (int i = 0; i < n; i++) {
        t.emplace_back(i, i, 2.0);
        if (i + 1 < n) {
            t.emplace_back(i, i + 1, -1.0);
            t.emplace_back(i + 1, i, -1.0);
        }
    }
    Eigen::SparseMatrix<double> m(n, n);
    m.setFromTriplets(t.begin(), t.end());
    SparseSymMatProd<double> op(m);
    SymEigsSolver<SparseSymMatProd<double>> eigs(op, 3, 6);
    eigs.init();
    if (eigs.compute(SortRule::LargestAlge) != 3 || eigs.info() != CompInfo::Successful)
        return fail("sparse symmetric status");
    const double pi = std::acos(-1.0);
    for (int k = 0; k < 3; k++) {
        // Eigenvalues of the 1-D Laplacian: 2 - 2 cos(j pi / (n + 1)).
        if (!close(eigs.eigenvalues()(k), 2 - 2 * std::cos((10 - k) * pi / 11), 1e-8))
            return fail("sparse symmetric eigenvalue");
    }
    return true;
}

static bool dense_general() {
    // Upper triangular: the eigenvalues are the diagonal 1..10.
    const int n = 10;
    Eigen::MatrixXd m = Eigen::MatrixXd::Zero(n, n);
    for (int i = 0; i < n; i++) {
        m(i, i) = i + 1;
        if (i + 1 < n) m(i, i + 1) = 1;
    }
    DenseGenMatProd<double> op(m);
    GenEigsSolver<DenseGenMatProd<double>> eigs(op, 3, 8);
    eigs.init();
    if (eigs.compute(SortRule::LargestMagn) != 3 || eigs.info() != CompInfo::Successful)
        return fail("general status");
    const Eigen::VectorXcd ev = eigs.eigenvalues();
    for (int k = 0; k < 3; k++) {
        if (!close(ev(k).real(), 10 - k, 1e-8) || !close(ev(k).imag(), 0, 1e-8))
            return fail("general eigenvalue");
    }
    return true;
}

int main() {
    return dense_symmetric() && sparse_symmetric() && dense_general() ? 0 : 1;
}
