// Checks Sophus's Lie groups over Eigen: SO(3) and SE(3) exp, log,
// composition, inverse and action on points, SO(2) angles, Sim(3) scaling,
// geodesic interpolation, and the eigen-solver rotation average.  The
// geometry, numerical-differentiation and spline headers are only parsed.
#include <cmath>
#include <cstdio>
#include <vector>

#include <Eigen/Core>
#include <sophus/average.hpp>
#include <sophus/geometry.hpp>
#include <sophus/interpolate.hpp>
#include <sophus/num_diff.hpp>
#include <sophus/se3.hpp>
#include <sophus/sim3.hpp>
#include <sophus/so2.hpp>
#include <sophus/spline.hpp>

namespace {

int failures = 0;

void expect(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "failed: %s\n", what);
    ++failures;
  }
}

bool approx(const Eigen::Vector3d& a, const Eigen::Vector3d& b) {
  return (a - b).norm() < 1e-9;
}

}  // namespace

int main() {
  const double pi = Sophus::Constants<double>::pi();

  const Sophus::SO3d quarter_z = Sophus::SO3d::rotZ(pi / 2);
  expect(approx(quarter_z * Eigen::Vector3d::UnitX(), Eigen::Vector3d::UnitY()),
         "SO3 rotates x onto y");

  const Eigen::Vector3d omega(0.1, -0.2, 0.3);
  const Sophus::SO3d r = Sophus::SO3d::exp(omega);
  expect(approx(r.log(), omega), "SO3 log inverts exp");
  expect(std::abs(r.matrix().determinant() - 1) < 1e-9, "SO3 matrix");

  // Rotate, then translate: (1, 0, 0) -> (0, 1, 0) + (1, 2, 3).
  const Sophus::SE3d t(quarter_z, Eigen::Vector3d(1, 2, 3));
  expect(approx(t * Eigen::Vector3d::UnitX(), Eigen::Vector3d(1, 3, 3)),
         "SE3 action");
  expect(((t * t.inverse()).matrix() - Eigen::Matrix4d::Identity()).norm() <
             1e-9,
         "SE3 inverse");
  expect((Sophus::SE3d::exp(t.log()).matrix() - t.matrix()).norm() < 1e-9,
         "SE3 log inverts exp");

  expect(std::abs((Sophus::SO2d(0.5) * Sophus::SO2d(0.25)).log() - 0.75) <
             1e-9,
         "SO2 adds angles");

  const Sophus::Sim3d s(Sophus::RxSO3d(2.0, Sophus::SO3d()),
                        Eigen::Vector3d::Zero());
  expect(std::abs(s.scale() - 2) < 1e-9 &&
             approx(s * Eigen::Vector3d(1, 2, 3), Eigen::Vector3d(2, 4, 6)),
         "Sim3 scales");

  // Halfway along the geodesic from the identity to a quarter turn about z.
  const Sophus::SO3d eighth = Sophus::interpolate(Sophus::SO3d(), quarter_z);
  expect(approx(eighth.log(), Eigen::Vector3d(0, 0, pi / 4)),
         "SO3 interpolation");

  // Rotations of 0.1 and 0.3 rad about z average to 0.2 rad.
  const std::vector<Sophus::SO3d> rotations = {Sophus::SO3d::rotZ(0.1),
                                               Sophus::SO3d::rotZ(0.3)};
  const auto mean = Sophus::average(rotations);
  expect(mean.has_value() && approx(mean->log(), Eigen::Vector3d(0, 0, 0.2)),
         "SO3 average");

  if (failures != 0) return 1;
  std::puts("sophus OK");
  return 0;
}
