#include <TinyAD/ScalarFunction.hh>
#include <TinyAD/Utils/Helpers.hh>
#include <TinyAD/Utils/NewtonDirection.hh>

#include <cmath>
#include <cstdio>
#include <cstdlib>

#define CHECK(cond)                                                    \
  do {                                                                 \
    if (!(cond)) {                                                     \
      std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__,      \
                   __LINE__, #cond);                                   \
      std::exit(1);                                                    \
    }                                                                  \
  } while (0)

static bool near(double a, double b) { return std::abs(a - b) < 1e-12; }

int main() {
  // f(x, y) = x*x*y + sin(y) at (2, 1).
  {
    using D = TinyAD::Double<2>;
    Eigen::Vector2d p(2.0, 1.0);
    Eigen::Matrix<D, 2, 1> v = D::make_active(p);
    D f = v[0] * v[0] * v[1] + sin(v[1]);
    CHECK(near(f.val, 4.0 + std::sin(1.0)));
    CHECK(near(f.grad[0], 4.0));                  // 2xy
    CHECK(near(f.grad[1], 4.0 + std::cos(1.0)));  // x^2 + cos y
    CHECK(near(f.Hess(0, 0), 2.0));               // 2y
    CHECK(near(f.Hess(0, 1), 4.0));               // 2x
    CHECK(near(f.Hess(1, 0), 4.0));
    CHECK(near(f.Hess(1, 1), -std::sin(1.0)));    // -sin y
  }

  // Two 2D points p0, p1: |p0 - p1|^2 + |p0 - t0|^2 + |p1 - t1|^2.
  const Eigen::Vector2d t0(0.0, 0.0);
  const Eigen::Vector2d t1(2.0, 0.0);
  auto func = TinyAD::scalar_function<2>(TinyAD::range(2));
  func.add_elements<2>(TinyAD::range(1), [](auto& element) {
    using T = TINYAD_SCALAR_TYPE(element);
    Eigen::Vector2<T> a = element.variables(0);
    Eigen::Vector2<T> b = element.variables(1);
    return (a - b).squaredNorm();
  });
  func.add_elements<1>(TinyAD::range(2), [&](auto& element) {
    using T = TINYAD_SCALAR_TYPE(element);
    Eigen::Vector2<T> p = element.variables(element.handle);
    Eigen::Vector2d t = element.handle == 0 ? t0 : t1;
    return (p - t).squaredNorm();
  });

  Eigen::VectorXd x(4);
  x << 1.0, 1.0, 3.0, -1.0;
  const double expected_g[4] = {-2.0, 6.0, 6.0, -6.0};
  // Hessian [[4I, -2I], [-2I, 4I]], already positive definite.
  Eigen::MatrixXd expected_H(4, 4);
  expected_H << 4, 0, -2, 0,  //
      0, 4, 0, -2,            //
      -2, 0, 4, 0,            //
      0, -2, 0, 4;

  CHECK(near(func.eval(x), 12.0));
  {
    auto [f, g, H] = func.eval_with_derivatives(x);
    CHECK(near(f, 12.0));
    for (int i = 0; i < 4; ++i) CHECK(near(g[i], expected_g[i]));
    CHECK((Eigen::MatrixXd(H) - expected_H).cwiseAbs().maxCoeff() < 1e-12);
  }
  // Eigenvalue clamp 0: the pair term's singular Hessian is not shifted.
  auto [f, g, H_proj] = func.eval_with_hessian_proj(x, 0.0);
  CHECK(near(f, 12.0));
  CHECK((Eigen::MatrixXd(H_proj) - expected_H).cwiseAbs().maxCoeff() < 1e-12);

  // One Newton step on the quadratic reaches its minimum, p0 = (2/3, 0) and
  // p1 = (4/3, 0).
  Eigen::VectorXd x_new = x + TinyAD::newton_direction(g, H_proj);
  CHECK(near(x_new[0], 2.0 / 3.0));
  CHECK(near(x_new[1], 0.0));
  CHECK(near(x_new[2], 4.0 / 3.0));
  CHECK(near(x_new[3], 0.0));
  CHECK(std::get<1>(func.eval_with_gradient(x_new)).norm() < 1e-12);
  return 0;
}
