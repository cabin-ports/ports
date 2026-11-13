/* Each test runs through the inline (glm_), struct (glms_) and compiled (glmc_) APIs.  Expected
 * values come from a plain column-major product computed here, or by hand: a 90-degree turn about
 * z takes x to y; the OpenGL perspective with a 90-degree fovy, aspect 2, near 1 and far 3 has
 * entries 1/2, 1, (1+3)/(1-3) = -2, -1 and 2*1*3/(1-3) = -3, and maps z = -1 and z = -3 to
 * NDC depth -1 and 1. */
#include <cglm/call.h>
#include <cglm/cglm.h>
#include <cglm/struct.h>

#include <math.h>
#include <stdio.h>

static int failures = 0;

static void check(int ok, const char *what, int line) {
  if (!ok) {
    fprintf(stderr, "main.c:%d: check failed: %s\n", line, what);
    ++failures;
  }
}
#define CHECK(x) check((x), #x, __LINE__)

static int approx(float actual, float expected, float tolerance) {
  return fabsf(actual - expected) <= tolerance;
}

/* Column c, row r: m[c][r], as cglm stores matrices. */
static void fill(mat4 m, float first) {
  int c, r;
  for (c = 0; c < 4; ++c)
    for (r = 0; r < 4; ++r)
      m[c][r] = first + (float)(4 * c + r);
}

static void reference_mul(mat4 a, mat4 b, mat4 out) {
  int c, r, k;
  for (c = 0; c < 4; ++c)
    for (r = 0; r < 4; ++r) {
      float sum = 0.0f;
      for (k = 0; k < 4; ++k)
        sum += a[k][r] * b[c][k];
      out[c][r] = sum;
    }
}

static int equal_exactly(mat4 a, mat4 b) {
  int c, r;
  for (c = 0; c < 4; ++c)
    for (r = 0; r < 4; ++r)
      if (a[c][r] != b[c][r])
        return 0;
  return 1;
}

static int near_identity(mat4 m, float tolerance) {
  int c, r;
  for (c = 0; c < 4; ++c)
    for (r = 0; r < 4; ++r)
      if (!approx(m[c][r], c == r ? 1.0f : 0.0f, tolerance))
        return 0;
  return 1;
}

/* Integer products below 2^24 are exact in float, whatever the SIMD path. */
static void test_multiply(void) {
  mat4 a, b, want, got;
  mat4s as, bs, gots;
  fill(a, 1.0f);
  fill(b, 17.0f);
  reference_mul(a, b, want);
  CHECK(want[0][0] == 1*17 + 5*18 + 9*19 + 13*20);

  glm_mat4_mul(a, b, got);
  CHECK(equal_exactly(got, want));

  glm_mat4_copy(a, as.raw);
  glm_mat4_copy(b, bs.raw);
  gots = glms_mat4_mul(as, bs);
  CHECK(equal_exactly(gots.raw, want));

  glmc_mat4_mul(a, b, got);
  CHECK(equal_exactly(got, want));

  /* Translate after scale: (1, 1, 1) -> (2*1 + 1, 3*1 + 2, 4*1 + 3). */
  {
    vec3 t = {1.0f, 2.0f, 3.0f}, s = {2.0f, 3.0f, 4.0f};
    vec4 p = {1.0f, 1.0f, 1.0f, 1.0f}, q;
    mat4 tm, sm, ts;
    glm_translate_make(tm, t);
    glm_scale_make(sm, s);
    glmc_mat4_mul(tm, sm, ts);
    glmc_mat4_mulv(ts, p, q);
    CHECK(q[0] == 3.0f && q[1] == 5.0f && q[2] == 7.0f && q[3] == 1.0f);
  }
}

/* M = translate(1, 2, 3) * scale(2, 4, 8); its inverse scales by 1/2, 1/4, 1/8 and moves the
 * origin to (-1/2, -2/4, -3/8). */
static void test_inverse(void) {
  vec3 t = {1.0f, 2.0f, 3.0f}, s = {2.0f, 4.0f, 8.0f};
  mat4 tm, sm, m, inv, prod;
  mat4s ms, invs;
  const float tol = 1e-6f;
  glm_translate_make(tm, t);
  glm_scale_make(sm, s);
  glm_mat4_mul(tm, sm, m);

  glm_mat4_inv(m, inv);
  CHECK(approx(inv[0][0], 0.5f, tol) && approx(inv[1][1], 0.25f, tol) && approx(inv[2][2], 0.125f, tol));
  CHECK(approx(inv[3][0], -0.5f, tol) && approx(inv[3][1], -0.5f, tol) && approx(inv[3][2], -0.375f, tol));
  glm_mat4_mul(m, inv, prod);
  CHECK(near_identity(prod, tol));

  glm_mat4_copy(m, ms.raw);
  invs = glms_mat4_inv(ms);
  CHECK(approx(invs.raw[3][2], -0.375f, tol) && approx(invs.raw[2][2], 0.125f, tol));

  glmc_mat4_inv(m, inv);
  glmc_mat4_mul(inv, m, prod);
  CHECK(near_identity(prod, tol));
  CHECK(approx(inv[3][1], -0.5f, tol));
}

static void test_quaternion(void) {
  vec3 z = {0.0f, 0.0f, 1.0f}, x = {1.0f, 0.0f, 0.0f}, y = {0.0f, 1.0f, 0.0f}, out;
  versor q;
  versors qs;
  vec3s zs, xs, outs;
  const float tol = 1e-6f;

  glm_quatv(q, GLM_PI_2f, z);
  /* (0, 0, sin 45 deg, cos 45 deg): the axis part first, the scalar last. */
  CHECK(approx(q[0], 0.0f, tol) && approx(q[1], 0.0f, tol));
  CHECK(approx(q[2], 0.70710678f, tol) && approx(q[3], 0.70710678f, tol));
  glm_quat_rotatev(q, x, out);
  CHECK(approx(out[0], 0.0f, tol) && approx(out[1], 1.0f, tol) && approx(out[2], 0.0f, tol));

  glm_vec3_copy(z, zs.raw);
  qs = glms_quatv(GLM_PI_2f, zs);
  glm_vec3_copy(x, xs.raw);
  outs = glms_quat_rotatev(qs, xs);
  CHECK(approx(outs.raw[0], 0.0f, tol) && approx(outs.raw[1], 1.0f, tol));

  /* A half turn about y takes x to -x. */
  glmc_quatv(q, GLM_PIf, y);
  glmc_quat_rotatev(q, x, out);
  CHECK(approx(out[0], -1.0f, tol) && approx(out[1], 0.0f, tol) && approx(out[2], 0.0f, tol));
}

static void test_perspective(void) {
  mat4 p;
  mat4s ps;
  vec4 nearp = {0.0f, 0.0f, -1.0f, 1.0f}, farp = {0.0f, 0.0f, -3.0f, 1.0f}, clip;
  const float tol = 1e-6f;

  glm_perspective(GLM_PI_2f, 2.0f, 1.0f, 3.0f, p);
  CHECK(approx(p[0][0], 0.5f, tol) && approx(p[1][1], 1.0f, tol));
  CHECK(approx(p[2][2], -2.0f, tol) && approx(p[2][3], -1.0f, tol) && approx(p[3][2], -3.0f, tol));
  CHECK(p[3][3] == 0.0f && p[0][1] == 0.0f && p[1][0] == 0.0f);

  glm_mat4_mulv(p, nearp, clip);
  CHECK(approx(clip[2] / clip[3], -1.0f, tol));
  glm_mat4_mulv(p, farp, clip);
  CHECK(approx(clip[2] / clip[3], 1.0f, tol));

  ps = glms_perspective(GLM_PI_2f, 2.0f, 1.0f, 3.0f);
  CHECK(approx(ps.raw[0][0], 0.5f, tol) && approx(ps.raw[3][2], -3.0f, tol));

  glmc_perspective(GLM_PI_2f, 2.0f, 1.0f, 3.0f, p);
  CHECK(approx(p[2][2], -2.0f, tol) && approx(p[3][2], -3.0f, tol));
}

int main(void) {
  test_multiply();
  test_inverse();
  test_quaternion();
  test_perspective();
  if (failures) {
    fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  printf("cglm: ok\n");
  return 0;
}
