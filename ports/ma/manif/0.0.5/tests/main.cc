// NDEBUG as in a release build, which patch 0001 needs.
#ifndef NDEBUG
#define NDEBUG
#endif

#include <manif/manif.h>

#include <cmath>
#include <cstdio>

namespace {

int failures = 0;

#define CHECK(cond)                                              \
  do {                                                           \
    if (!(cond)) {                                               \
      std::printf("%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); \
      ++failures;                                                \
    }                                                            \
  } while (0)

const double kPi = 3.14159265358979323846;
const double kTol = 1e-9;

template <typename A, typename B>
bool near(const A& a, const B& b, double tol = kTol)
{
  return (a - b).norm() < tol;
}

// Central finite difference of f(m (+) d) with respect to the right
// perturbation d, where f returns a vector.
template <typename G, typename F>
Eigen::MatrixXd fd_jacobian(const G& m, F f)
{
  const double eps = 1e-6;
  const int n = G::DoF;
  Eigen::MatrixXd J;
  for (int i = 0; i < n; ++i) {
    typename G::Tangent::DataType d = G::Tangent::DataType::Zero();
    d(i) = eps;
    const Eigen::VectorXd hi = f(m.rplus(typename G::Tangent(d)));
    d(i) = -eps;
    const Eigen::VectorXd lo = f(m.rplus(typename G::Tangent(d)));
    if (i == 0) J.resize(hi.size(), n);
    J.col(i) = (hi - lo) / (2 * eps);
  }
  return J;
}

// Jacobians of inverse, compose, act and log against finite differences, and
// the exp/log round trip, for one group element pair.
template <typename G>
void check_jacobians(const G& a, const G& b, const typename G::Vector& p)
{
  const double tol = 1e-6;
  typename G::Jacobian J, Ja, Jb;
  Eigen::Matrix<double, G::Dim, G::DoF> Jm;
  Eigen::Matrix<double, G::Dim, G::Dim> Jv;

  a.inverse(J);
  CHECK(near(J, fd_jacobian(a, [&](const G& g) -> Eigen::VectorXd {
                    return g.inverse().rminus(a.inverse()).coeffs();
                  }), tol));

  a.compose(b, Ja, Jb);
  const G ab = a.compose(b);
  CHECK(near(Ja, fd_jacobian(a, [&](const G& g) -> Eigen::VectorXd {
                    return g.compose(b).rminus(ab).coeffs();
                  }), tol));
  CHECK(near(Jb, fd_jacobian(b, [&](const G& g) -> Eigen::VectorXd {
                    return a.compose(g).rminus(ab).coeffs();
                  }), tol));

  const typename G::Vector q = a.act(p, Jm, Jv);
  CHECK(near(Jm, fd_jacobian(a, [&](const G& g) -> Eigen::VectorXd {
                    return g.act(p) - q;
                  }), tol));
  CHECK(Jv.isApprox(a.rotation(), 1e-9));

  typename G::Jacobian Jl;
  const auto t = a.log(Jl);
  CHECK(near(Jl, fd_jacobian(a, [&](const G& g) -> Eigen::VectorXd {
                    return g.log().coeffs() - t.coeffs();
                  }), tol));

  CHECK(near(t.exp().coeffs(), a.coeffs()));
  CHECK(near(a.compose(a.inverse()).coeffs(), G::Identity().coeffs()));
}

void test_so2()
{
  manif::SO2d r(kPi / 2);
  CHECK(std::abs(r.angle() - kPi / 2) < kTol);
  CHECK(near(r.act(Eigen::Vector2d(1, 0)), Eigen::Vector2d(0, 1)));
  CHECK(std::abs(r.compose(r).angle() - kPi) < kTol);
  CHECK(std::abs(r.inverse().angle() + kPi / 2) < kTol);
  CHECK(std::abs(r.log().angle() - kPi / 2) < kTol);
  CHECK(std::abs(manif::SO2Tangentd(0.25).exp().angle() - 0.25) < kTol);
  check_jacobians(manif::SO2d(0.3), manif::SO2d(-1.1), Eigen::Vector2d(0.7, -0.4));
}

void test_se2()
{
  manif::SE2d g(1, 2, kPi / 2);
  CHECK(near(g.act(Eigen::Vector2d(1, 0)), Eigen::Vector2d(1, 3)));
  const manif::SE2d gg = g.compose(g);  // rotate (1,2) by 90 degrees, add (1,2)
  CHECK(near(gg.translation(), Eigen::Vector2d(-1, 3)));
  CHECK(std::abs(gg.angle() - kPi) < kTol);
  CHECK(near(g.inverse().translation(), Eigen::Vector2d(-2, 1)));
  // exp of (v = (1, 0), w = pi/2): translation V v = (2/pi, 2/pi).
  const manif::SE2d e = manif::SE2Tangentd(1, 0, kPi / 2).exp();
  CHECK(near(e.translation(), Eigen::Vector2d(2 / kPi, 2 / kPi)));
  CHECK(std::abs(e.angle() - kPi / 2) < kTol);
  check_jacobians(manif::SE2d(0.5, -1.0, 0.3), manif::SE2d(-2.0, 0.25, 1.2),
                  Eigen::Vector2d(0.7, -0.4));
}

void test_so3()
{
  const manif::SO3d rz(Eigen::AngleAxisd(kPi / 2, Eigen::Vector3d::UnitZ()));
  CHECK(near(rz.act(Eigen::Vector3d(1, 0, 0)), Eigen::Vector3d(0, 1, 0)));
  CHECK(near(rz.compose(rz).act(Eigen::Vector3d(1, 0, 0)), Eigen::Vector3d(-1, 0, 0)));
  CHECK(near(rz.inverse().act(Eigen::Vector3d(0, 1, 0)), Eigen::Vector3d(1, 0, 0)));
  const manif::SO3d r(Eigen::AngleAxisd(0.5, Eigen::Vector3d::UnitZ()));
  CHECK(near(r.log().coeffs(), Eigen::Vector3d(0, 0, 0.5)));
  CHECK(near(manif::SO3Tangentd(Eigen::Vector3d(0.5, 0, 0)).exp().rotation(),
             Eigen::AngleAxisd(0.5, Eigen::Vector3d::UnitX()).toRotationMatrix()));
  check_jacobians(manif::SO3d(Eigen::AngleAxisd(0.7, Eigen::Vector3d(1, 2, 3).normalized())),
                  manif::SO3d(0.1, -0.2, 0.3), Eigen::Vector3d(0.7, -0.4, 1.5));
}

void test_se3()
{
  const Eigen::Quaterniond qz(Eigen::AngleAxisd(kPi / 2, Eigen::Vector3d::UnitZ()));
  const manif::SE3d g(Eigen::Vector3d(1, 2, 3), qz);
  CHECK(near(g.act(Eigen::Vector3d(1, 0, 0)), Eigen::Vector3d(1, 3, 3)));
  CHECK(near(g.inverse().translation(), Eigen::Vector3d(-2, 1, -3)));
  CHECK(near(g.compose(g).translation(), Eigen::Vector3d(-1, 3, 6)));
  // Pure translation tangent exponentiates to the translation itself.
  CHECK(near(manif::SE3Tangentd((Eigen::Matrix<double, 6, 1>() << 1, 2, 3, 0, 0, 0).finished())
                 .exp().translation(),
             Eigen::Vector3d(1, 2, 3)));
  const manif::SE3d a(Eigen::Vector3d(0.5, -1, 2),
                      Eigen::Quaterniond(Eigen::AngleAxisd(0.4, Eigen::Vector3d(1, 1, 0).normalized())));
  const manif::SE3d b(Eigen::Vector3d(-1, 0.3, 0.2),
                      Eigen::Quaterniond(Eigen::AngleAxisd(-1.1, Eigen::Vector3d(0, 1, 1).normalized())));
  check_jacobians(a, b, Eigen::Vector3d(0.7, -0.4, 1.5));
}

}  // namespace

int main()
{
  test_so2();
  test_se2();
  test_so3();
  test_se3();
  if (failures != 0) {
    std::printf("%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("ok");
  return 0;
}
