// Solves a 2D Poisson problem with a known solution through the static and the
// ptree-configured interfaces, and checks convergence and the solution.
#include <amgcl/adapter/crs_tuple.hpp>
#include <amgcl/amg.hpp>
#include <amgcl/backend/builtin.hpp>
#include <amgcl/coarsening/smoothed_aggregation.hpp>
#include <amgcl/make_solver.hpp>
#include <amgcl/relaxation/spai0.hpp>
#include <amgcl/solver/bicgstab.hpp>
#include <amgcl/solver/cg.hpp>

#include <boost/property_tree/ptree.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <tuple>
#include <vector>

using Backend = amgcl::backend::builtin<double>;
using Precond = amgcl::amg<Backend, amgcl::coarsening::smoothed_aggregation, amgcl::relaxation::spai0>;

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

static const ptrdiff_t m = 40, n = m * m;
static std::vector<ptrdiff_t> ptr(1, 0), col;
static std::vector<double> val;

template <class Solver>
static void check(const char *name, const Solver &solve, const std::vector<double> &exact,
                  const std::vector<double> &rhs, size_t max_iters) {
    std::vector<double> x(n, 0.0);
    size_t iters;
    double error;
    std::tie(iters, error) = solve(rhs, x);

    double worst = 0;
    for (ptrdiff_t i = 0; i < n; ++i) worst = std::max(worst, std::fabs(x[i] - exact[i]));
    std::printf("%s: %zu iterations, residual %g, max error %g\n", name, iters, error, worst);
    CHECK(iters > 0 && iters <= max_iters);
    CHECK(error < 1e-10);
    CHECK(worst < 1e-6);
}

int main() {
    // 5-point Laplacian on an m x m grid, Dirichlet boundary.
    for (ptrdiff_t j = 0; j < m; ++j)
        for (ptrdiff_t i = 0; i < m; ++i) {
            auto add = [&](ptrdiff_t ii, ptrdiff_t jj, double v) {
                if (ii < 0 || ii >= m || jj < 0 || jj >= m) return;
                col.push_back(jj * m + ii);
                val.push_back(v);
            };
            add(i, j - 1, -1);
            add(i - 1, j, -1);
            add(i, j, 4);
            add(i + 1, j, -1);
            add(i, j + 1, -1);
            ptr.push_back(col.size());
        }

    std::vector<double> exact(n), rhs(n, 0.0);
    for (ptrdiff_t i = 0; i < n; ++i) exact[i] = 1.0 + 0.25 * (i % 7);
    for (ptrdiff_t r = 0; r < n; ++r)
        for (ptrdiff_t k = ptr[r]; k < ptr[r + 1]; ++k) rhs[r] += val[k] * exact[col[k]];
    CHECK(ptr.back() == (ptrdiff_t)val.size() && val.size() == 5 * n - 4 * m);

    // Static interface, parameters set directly.
    {
        using Solver = amgcl::make_solver<Precond, amgcl::solver::bicgstab<Backend>>;
        Solver::params prm;
        prm.solver.tol = 1e-12;
        prm.precond.coarse_enough = 100; // force a multilevel hierarchy
        Solver solve(std::tie(n, ptr, col, val), prm);
        check("bicgstab", solve, exact, rhs, 30);
    }

    // Static interface, parameters from a Boost.PropertyTree.
    {
        using Solver = amgcl::make_solver<Precond, amgcl::solver::cg<Backend>>;
        boost::property_tree::ptree p;
        p.put("solver.tol", 1e-12);
        p.put("solver.maxiter", 50);
        p.put("precond.coarse_enough", 100);
        Solver::params prm(p);
        CHECK(prm.solver.maxiter == 50 && prm.precond.coarse_enough == 100);
        Solver solve(std::tie(n, ptr, col, val), prm);
        check("cg", solve, exact, rhs, 50);

        boost::property_tree::ptree out;
        solve.get_params(out);
        CHECK(out.get<double>("solver.tol") == 1e-12);
        CHECK(out.get<int>("precond.coarse_enough") == 100);
    }

    // A too-small iteration cap must be reported rather than hidden.
    {
        using Solver = amgcl::make_solver<Precond, amgcl::solver::cg<Backend>>;
        Solver::params prm;
        prm.solver.tol = 1e-12;
        prm.solver.maxiter = 1;
        prm.precond.coarse_enough = 100;
        Solver solve(std::tie(n, ptr, col, val), prm);
        std::vector<double> x(n, 0.0);
        size_t iters;
        double error;
        std::tie(iters, error) = solve(rhs, x);
        CHECK(iters == 1 && error > 1e-12);
    }

    return failures != 0;
}
