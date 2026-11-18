/* simd4f_neon.h is C++-only (see cabin.toml), so C on NEON uses the GNU
   backend. */
#if (defined(__ARM_NEON) || defined(__ARM_NEON__)) && defined(__GNUC__)
#define VECTORIAL_FORCED
#define VECTORIAL_GNU
#endif

#include <vectorial/simd4x4f.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond)                                                            \
  do {                                                                         \
    if (!(cond)) {                                                             \
      fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond); \
      ++failures;                                                              \
    }                                                                          \
  } while (0)

static int equals(simd4f v, float x, float y, float z, float w) {
  return simd4f_get_x(v) == x && simd4f_get_y(v) == y &&
         simd4f_get_z(v) == z && simd4f_get_w(v) == w;
}

static int near(simd4f v, float x, float y, float z, float w) {
  return fabsf(simd4f_get_x(v) - x) < 1e-5f &&
         fabsf(simd4f_get_y(v) - y) < 1e-5f &&
         fabsf(simd4f_get_z(v) - z) < 1e-5f &&
         fabsf(simd4f_get_w(v) - w) < 1e-5f;
}

int main(void) {
  const simd4f v = simd4f_create(1.0f, 2.0f, 3.0f, 4.0f);
  simd4f c;
  simd4x4f m;
  volatile float angle = 0.785398163f;

  CHECK(equals(simd4f_dot3(v, v), 14, 14, 14, 14));
  CHECK(simd4f_get_x(simd4f_sum(v)) == 10.0f);
  CHECK(simd4f_get_w(v) == 4.0f);

  c = simd4f_cross3(simd4f_create(1, 0, 0, 0), simd4f_create(0, 1, 0, 0));
  CHECK(simd4f_get_x(c) == 0.0f && simd4f_get_y(c) == 0.0f &&
        simd4f_get_z(c) == 1.0f);
  c = simd4f_cross3(simd4f_create(1, 2, 3, 0), simd4f_create(4, 5, 6, 0));
  CHECK(equals(c, -3, 6, -3, 0));

  /* sinf and cosf with a runtime angle: 45 degrees about (1, 2, 3), as in
     upstream spec/spec_simd4x4f.cpp:366-373. */
  simd4x4f_axis_rotation(&m, angle, simd4f_create(1, 2, 3, 0));
  CHECK(near(m.x, 0.728028f, 0.608789f, -0.315202f, 0));
  CHECK(near(m.y, -0.525105f, 0.790791f, 0.314508f, 0));
  CHECK(near(m.z, 0.440727f, -0.0634566f, 0.895395f, 0));
  CHECK(equals(m.w, 0, 0, 0, 1));

#if defined(__x86_64__) || defined(_M_X64)
  CHECK(strcmp(VECTORIAL_SIMD_TYPE, "sse") == 0);
#elif defined(VECTORIAL_GNU)
  CHECK(strcmp(VECTORIAL_SIMD_TYPE, "gnu") == 0);
#endif

  if (failures != 0) {
    fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  printf("vectorial ok (C, %s)\n", VECTORIAL_SIMD_TYPE);
  return 0;
}
