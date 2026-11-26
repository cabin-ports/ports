#include <cmath>
#include <cstdio>

#include "mathfu/glsl_mappings.h"

namespace {

int failures = 0;

#define CHECK(cond)                                                            \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__,    \
                   #cond);                                                     \
      ++failures;                                                              \
    }                                                                          \
  } while (0)

const float kEps = 1e-5f;
const float kHalfPi = 1.57079632679489661923f;

bool near(const mathfu::vec3& v, float x, float y, float z) {
  return std::fabs(v[0] - x) < kEps && std::fabs(v[1] - y) < kEps &&
         std::fabs(v[2] - z) < kEps;
}

} // namespace

int main() {
  std::printf("%s\n", MATHFU_BUILD_OPTIONS_STRING);

  // Vectors.
  const mathfu::vec3 a(1, 2, 3);
  const mathfu::vec3 b(4, 5, 6);
  CHECK(mathfu::vec3::DotProduct(a, b) == 32.0f);
  CHECK(near(mathfu::vec3::CrossProduct(a, b), -3, 6, -3));
  CHECK(near(a + b, 5, 7, 9));
  CHECK(a.LengthSquared() == 14.0f);
  const mathfu::vec3 c(3, 0, 4);
  CHECK(std::fabs(c.Length() - 5.0f) < kEps);
  CHECK(near(c.Normalized(), 0.6f, 0, 0.8f));

  // Matrices: M = translate(1, 2, 3) * scale(2, 4, 5).
  const mathfu::mat4 m =
      mathfu::mat4::FromTranslationVector(mathfu::vec3(1, 2, 3)) *
      mathfu::mat4::FromScaleVector(mathfu::vec3(2, 4, 5));
  CHECK(near(m * mathfu::vec3(1, 1, 1), 3, 6, 8));
  const mathfu::mat4 product = m * m.Inverse();
  for (int row = 0; row < 4; ++row) {
    for (int col = 0; col < 4; ++col) {
      CHECK(std::fabs(product(row, col) - (row == col ? 1.0f : 0.0f)) < kEps);
    }
  }
  CHECK(near(m.Inverse() * mathfu::vec3(3, 6, 8), 1, 1, 1));
  CHECK(near(m.Inverse() * mathfu::vec3(0, 0, 0), -0.5f, -0.5f, -0.6f));

  // Quaternions: a quarter turn about +z, then two of them.
  const mathfu::quat q =
      mathfu::quat::FromAngleAxis(kHalfPi, mathfu::vec3(0, 0, 1));
  CHECK(near(q * mathfu::vec3(1, 0, 0), 0, 1, 0));
  CHECK(near((q * q) * mathfu::vec3(1, 0, 0), -1, 0, 0));
  CHECK(near(q.Inverse() * mathfu::vec3(0, 1, 0), 1, 0, 0));

  // Right-handed look-at from (0, 0, 5) towards the origin.
  const mathfu::mat4 view = mathfu::mat4::LookAt(
      mathfu::vec3(0, 0, 0), mathfu::vec3(0, 0, 5), mathfu::vec3(0, 1, 0), 1);
  CHECK(near(view * mathfu::vec3(0, 0, 5), 0, 0, 0));
  CHECK(near(view * mathfu::vec3(0, 0, 0), 0, 0, -5));
  CHECK(near(view * mathfu::vec3(1, 2, 0), 1, 2, -5));

  // Perspective with a 90 degree fovy and aspect 2: x scales by 1/2, y by 1,
  // and w is the eye-space depth.
  const mathfu::mat4 proj =
      mathfu::mat4::Perspective(kHalfPi, 2.0f, 1.0f, 3.0f);
  const mathfu::vec4 clip = proj * mathfu::vec4(2, 1, -2, 1);
  CHECK(std::fabs(clip[0] - 1.0f) < kEps);
  CHECK(std::fabs(clip[1] - 1.0f) < kEps);
  CHECK(std::fabs(clip[3] - 2.0f) < kEps);

  return failures == 0 ? 0 : 1;
}
