// The C++ consumer: the header through extern "C" from C++, the elimination
// tree of a 4x4 quasidefinite matrix whose tree branches, its LDL' factors
// and solve in std::vector storage, and QDLDL_Lsolve/QDLDL_Ltsolve composing
// to the same solution as QDLDL_solve.
#include <qdldl.h>

#include <cmath>
#include <cstdio>
#include <vector>

static bool ok = true;

static void check(bool pass, const char *what) {
    if (!pass) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ok = false;
    }
}

static bool near(double a, double b) { return std::fabs(a - b) < 1e-12; }

int main() {
    // Upper triangle of [[2,0,1,0],[0,3,0,1],[1,0,4,1],[0,1,1,-5]].
    const QDLDL_int n = 4;
    const std::vector<QDLDL_int> Ap = {0, 1, 2, 4, 7};
    const std::vector<QDLDL_int> Ai = {0, 1, 0, 2, 1, 2, 3};
    const std::vector<QDLDL_float> Ax = {2, 3, 1, 4, 1, 1, -5};

    std::vector<QDLDL_int> work(n), Lnz(n), etree(n);
    check(QDLDL_etree(n, Ap.data(), Ai.data(), work.data(), Lnz.data(), etree.data()) == 3,
          "etree total");
    check(Lnz == std::vector<QDLDL_int>({1, 1, 1, 0}), "Lnz");
    check(etree == std::vector<QDLDL_int>({2, 3, 3, -1}), "etree");

    std::vector<QDLDL_int> Lp(n + 1), Li(3), iwork(3 * n);
    std::vector<QDLDL_float> Lx(3), D(n), Dinv(n), fwork(n);
    std::vector<QDLDL_bool> bwork(n);
    check(QDLDL_factor(n, Ap.data(), Ai.data(), Ax.data(), Lp.data(), Li.data(), Lx.data(), D.data(),
                       Dinv.data(), Lnz.data(), etree.data(), bwork.data(), iwork.data(),
                       fwork.data()) == 3,
          "positive pivots");
    check(Lp == std::vector<QDLDL_int>({0, 1, 2, 3, 3}), "Lp");
    check(Li == std::vector<QDLDL_int>({2, 3, 3}), "Li");
    check(near(Lx[0], 0.5) && near(Lx[1], 1.0 / 3.0) && near(Lx[2], 2.0 / 7.0), "Lx");
    check(near(D[0], 2) && near(D[1], 3) && near(D[2], 3.5) && near(D[3], -118.0 / 21.0), "D");
    check(near(Dinv[3], -21.0 / 118.0), "Dinv");

    const std::vector<QDLDL_float> b = {5, -2, 17, -19}; // A * {1, -2, 3, 4}
    const std::vector<QDLDL_float> expected = {1, -2, 3, 4};

    std::vector<QDLDL_float> x = b;
    QDLDL_solve(n, Lp.data(), Li.data(), Lx.data(), Dinv.data(), x.data());
    for (int i = 0; i < n; ++i) check(near(x[i], expected[i]), "solve");

    std::vector<QDLDL_float> y = b;
    QDLDL_Lsolve(n, Lp.data(), Li.data(), Lx.data(), y.data());
    for (int i = 0; i < n; ++i) y[i] *= Dinv[i];
    QDLDL_Ltsolve(n, Lp.data(), Li.data(), Lx.data(), y.data());
    for (int i = 0; i < n; ++i) check(near(y[i], expected[i]), "Lsolve, D, Ltsolve");

    return ok ? 0 : 1;
}
