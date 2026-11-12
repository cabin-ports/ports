// Checks Realtime Math's vector, quaternion, transform and matrix arithmetic
// and its exp/log approximations, in float32 and float64.
#include <rtm/matrix3x4f.h>
#include <rtm/quatf.h>
#include <rtm/scalard.h>
#include <rtm/scalarf.h>
#include <rtm/vector4d.h>
#include <rtm/vector4f.h>
#include <rtm/vqvf.h>

#include <cmath>
#include <cstdio>

namespace {

int failures = 0;

void expect(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "failed: %s\n", what);
    ++failures;
  }
}

bool approx(double actual, double wanted, double tolerance) {
  return std::fabs(actual - wanted) <= tolerance * std::fmax(1.0, std::fabs(wanted));
}

}  // namespace

int main() {
  using namespace rtm;

  const vector4f a = vector_set(1.0F, 2.0F, 3.0F);
  const vector4f b = vector_set(4.0F, 5.0F, 6.0F);
  expect(vector_all_near_equal3(vector_add(a, b), vector_set(5.0F, 7.0F, 9.0F), 1.0E-6F), "add");
  expect(approx(float(vector_dot3(a, b)), 32.0, 1.0E-6), "dot3");
  expect(vector_all_near_equal3(vector_cross3(a, b), vector_set(-3.0F, 6.0F, -3.0F), 1.0E-6F), "cross3");
  expect(approx(float(vector_length3(vector_set(3.0F, 4.0F, 12.0F))), 13.0, 1.0E-6), "length3");

  // A quarter turn about +Z takes +X to +Y; twice that, to -X.
  const vector4f x_axis = vector_set(1.0F, 0.0F, 0.0F);
  const quatf quarter = quat_from_axis_angle(vector_set(0.0F, 0.0F, 1.0F), 1.57079632679489662F);
  expect(vector_all_near_equal3(quat_mul_vector3(x_axis, quarter), vector_set(0.0F, 1.0F, 0.0F), 1.0E-5F),
         "quarter turn");
  expect(vector_all_near_equal3(quat_mul_vector3(x_axis, quat_mul(quarter, quarter)),
                                vector_set(-1.0F, 0.0F, 0.0F), 1.0E-5F),
         "half turn");

  // Scale, then rotate, then translate: (1,0,0) -> (2,0,0) -> (0,2,0) -> (10,2,0).
  const vqvf transform = vqv_set(quarter, vector_set(10.0F, 0.0F, 0.0F), vector_set(2.0F, 2.0F, 2.0F));
  const vector4f moved = vqv_mul_point3(x_axis, transform);
  expect(vector_all_near_equal3(moved, vector_set(10.0F, 2.0F, 0.0F), 1.0E-5F), "transform");
  const matrix3x4f mtx = matrix_from_vqv(transform);
  expect(vector_all_near_equal3(matrix_mul_point3(x_axis, mtx), moved, 1.0E-5F), "matrix");
  expect(vector_all_near_equal3(matrix_mul_point3(moved, matrix_inverse(mtx)), x_axis, 1.0E-5F), "inverse");

  // The Cephes-based exp/log are within an ulp or two of the true values in
  // both precisions, and vector_exp agrees with scalar_exp in every lane.
  expect(approx(scalar_exp(1.0F), 2.718281828459045, 1.0E-6), "exp f32");
  expect(approx(scalar_log(10.0F), 2.302585092994046, 1.0E-6), "log f32");
  expect(approx(scalar_exp(1.0), 2.718281828459045, 1.0E-14), "exp f64");
  expect(approx(scalar_log(10.0), 2.302585092994046, 1.0E-14), "log f64");
  const vector4f exps = vector_exp(vector_set(-2.0F, 0.0F, 0.5F, 3.0F));
  expect(approx(float(vector_get_x(exps)), scalar_exp(-2.0F), 1.0E-6) &&
             approx(float(vector_get_y(exps)), 1.0, 1.0E-6) &&
             approx(float(vector_get_z(exps)), scalar_exp(0.5F), 1.0E-6) &&
             approx(float(vector_get_w(exps)), scalar_exp(3.0F), 1.0E-6),
         "vector exp f32");
  const vector4d logs = vector_log(vector_set(1.0, 2.0, 100.0, 0.25));
  expect(approx(double(vector_get_x(logs)), 0.0, 1.0E-14) &&
             approx(double(vector_get_y(logs)), 0.6931471805599453, 1.0E-14) &&
             approx(double(vector_get_z(logs)), 4.605170185988092, 1.0E-14) &&
             approx(double(vector_get_w(logs)), -1.3862943611198906, 1.0E-14),
         "vector log f64");

  if (failures != 0) return 1;
  std::puts("rtm OK");
  return 0;
}
