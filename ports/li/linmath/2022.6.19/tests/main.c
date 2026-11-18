/* Consumer test for cabin-ports/linmath from C: vector products and norms,
 * column-major 4x4 transforms (translation, products, inversion, look-at), and
 * quaternion rotation. */
#include <linmath.h>

#include <math.h>
#include <stdio.h>

static int failures = 0;

static void check(int pass, const char *what) {
  if (!pass) {
    fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

static int near4(vec4 const a, float x, float y, float z, float w) {
  return fabsf(a[0] - x) < 1e-5f && fabsf(a[1] - y) < 1e-5f && fabsf(a[2] - z) < 1e-5f && fabsf(a[3] - w) < 1e-5f;
}

static void test_vectors(void) {
  vec3 x = {1.f, 0.f, 0.f}, y = {0.f, 1.f, 0.f}, c, w = {3.f, 4.f, 0.f}, n;
  vec3_mul_cross(c, x, y);
  check(c[0] == 0.f && c[1] == 0.f && c[2] == 1.f, "x cross y is z");
  check(vec3_mul_inner(w, x) == 3.f && vec3_len(w) == 5.f, "inner product and length");
  vec3_norm(n, w);
  check(fabsf(n[0] - 0.6f) < 1e-6f && fabsf(n[1] - 0.8f) < 1e-6f && n[2] == 0.f, "normalization");
}

static void test_matrices(void) {
  mat4x4 T, U, P, I;
  vec4 v = {1.f, 2.f, 3.f, 1.f}, r, back;

  mat4x4_translate(T, 1.f, 2.f, 3.f);
  mat4x4_mul_vec4(r, T, v);
  check(r[0] == 2.f && r[1] == 4.f && r[2] == 6.f && r[3] == 1.f, "translation moves a point");

  mat4x4_translate(U, 4.f, 5.f, 6.f);
  mat4x4_mul(P, T, U);
  check(P[3][0] == 5.f && P[3][1] == 7.f && P[3][2] == 9.f && P[3][3] == 1.f, "translations compose");

  /* Scaling by 2 after translating differs from translating after scaling. */
  {
    mat4x4 E, S, TS, ST;
    vec4 p = {1.f, 1.f, 1.f, 1.f}, q;
    mat4x4_identity(E);
    mat4x4_scale_aniso(S, E, 2.f, 2.f, 2.f);
    mat4x4_mul(TS, T, S);
    mat4x4_mul(ST, S, T);
    mat4x4_mul_vec4(q, TS, p);
    check(q[0] == 3.f && q[1] == 4.f && q[2] == 5.f && q[3] == 1.f, "T * S scales first");
    mat4x4_mul_vec4(r, ST, p);
    check(r[0] == 4.f && r[1] == 6.f && r[2] == 8.f && r[3] == 1.f, "S * T translates first");

    mat4x4_invert(I, TS);
    mat4x4_mul_vec4(back, I, q);
    check(near4(back, 1.f, 1.f, 1.f, 1.f), "the inverse undoes the transform");
  }

  /* A camera at z = 5 looking at the origin sees it 5 units ahead, along -z. */
  {
    vec3 eye = {0.f, 0.f, 5.f}, center = {0.f, 0.f, 0.f}, up = {0.f, 1.f, 0.f};
    vec4 origin = {0.f, 0.f, 0.f, 1.f}, seen;
    mat4x4 L;
    mat4x4_look_at(L, eye, center, up);
    mat4x4_mul_vec4(seen, L, origin);
    check(near4(seen, 0.f, 0.f, -5.f, 1.f), "look-at view transform");
  }
}

static void test_quaternions(void) {
  quat q, id, qq;
  vec3 z = {0.f, 0.f, 1.f}, x = {1.f, 0.f, 0.f}, r;

  quat_rotate(q, 3.14159265f / 2.f, z);
  quat_mul_vec3(r, q, x);
  check(fabsf(r[0]) < 1e-6f && fabsf(r[1] - 1.f) < 1e-6f && fabsf(r[2]) < 1e-6f, "a quarter turn about z maps x to y");

  quat_identity(id);
  quat_mul(qq, q, id);
  check(qq[0] == q[0] && qq[1] == q[1] && qq[2] == q[2] && qq[3] == q[3], "the identity quaternion");

  /* p * q rotates by q first: y -> z about x, then z stays put about z; the
   * other order sends y to -x. */
  {
    quat qx, zx, xz;
    vec3 y = {0.f, 1.f, 0.f}, a, b;
    quat_rotate(qx, 3.14159265f / 2.f, x);
    quat_mul(zx, q, qx);
    quat_mul(xz, qx, q);
    quat_mul_vec3(a, zx, y);
    quat_mul_vec3(b, xz, y);
    check(fabsf(a[0]) < 1e-5f && fabsf(a[1]) < 1e-5f && fabsf(a[2] - 1.f) < 1e-5f, "composition order (z after x)");
    check(fabsf(b[0] + 1.f) < 1e-5f && fabsf(b[1]) < 1e-5f && fabsf(b[2]) < 1e-5f, "composition order (x after z)");
  }
}

int main(void) {
  test_vectors();
  test_matrices();
  test_quaternions();
  if (failures != 0) {
    fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  puts("linmath 2022.6.19 C ok");
  return 0;
}
