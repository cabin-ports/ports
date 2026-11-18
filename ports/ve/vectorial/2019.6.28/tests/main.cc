#include <vectorial/vectorial.h>
// A second inclusion of each vector header (patch 0001).
#include <vectorial/vec3f.h>
#include <vectorial/vec2f.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

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

bool near(const vectorial::vec3f& v, float x, float y, float z) {
  return std::fabs(v.x() - x) < 1e-5f && std::fabs(v.y() - y) < 1e-5f &&
         std::fabs(v.z() - z) < 1e-5f;
}

} // namespace

int main() {
  const vectorial::vec3f a(1, 2, 3);
  const vectorial::vec3f b(4, 5, 6);
  CHECK(vectorial::dot(a, b) == 32.0f);
  const vectorial::vec3f c = vectorial::cross(a, b);
  CHECK(c.x() == -3.0f && c.y() == 6.0f && c.z() == -3.0f);
  CHECK(simd4f_get_w(c.value) == 0.0f);
  // Estimate-based paths (rsqrt, reciprocal) are checked within 1e-5.
  CHECK(std::fabs(vectorial::length(a) - 3.741657f) < 1e-5f);
  CHECK(near(vectorial::normalize(a), 0.267261f, 0.534522f, 0.801784f));
  CHECK(near(40.0f / vectorial::vec3f(10, 20, 30), 4, 2, 1.333333f));
  const vectorial::vec3f s = a + b * 2.0f;
  CHECK(s.x() == 9.0f && s.y() == 12.0f && s.z() == 15.0f);

  // vec3f.h:177-180 adds these overloads to namespace std.
  const vectorial::vec3f lo = std::min(a, vectorial::vec3f(3, 2, 1));
  const vectorial::vec3f hi = std::max(a, vectorial::vec3f(3, 2, 1));
  CHECK(lo.x() == 1.0f && lo.y() == 2.0f && lo.z() == 1.0f);
  CHECK(hi.x() == 3.0f && hi.y() == 2.0f && hi.z() == 3.0f);

#if defined(__x86_64__) || defined(_M_X64)
  CHECK(std::strcmp(VECTORIAL_SIMD_TYPE, "sse") == 0);
#elif defined(__aarch64__)
  CHECK(std::strcmp(VECTORIAL_SIMD_TYPE, "neon") == 0);
#endif

  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::printf("vectorial ok (C++, %s)\n", VECTORIAL_SIMD_TYPE);
  return 0;
}
