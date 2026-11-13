// The C++ view of the same library: call.h and struct.h wrap their declarations in extern "C",
// so the compiled glmc_ functions link from C++.  Expected values as in main.c: an exact
// column-major product of small integers, a 90-degree turn about z taking x to y, and the
// OpenGL perspective with a 90-degree fovy, aspect 2, near 1 and far 3.
#include <cglm/call.h>
#include <cglm/cglm.h>
#include <cglm/struct.h>

#include <cmath>
#include <cstdio>

namespace {

int failures = 0;

void check(bool ok, const char *what, int line) {
    if (!ok) {
        std::fprintf(stderr, "main.cc:%d: check failed: %s\n", line, what);
        ++failures;
    }
}
#define CHECK(x) check((x), #x, __LINE__)

bool approx(float actual, float expected, float tolerance) {
    return std::fabs(actual - expected) <= tolerance;
}

void testMultiply() {
    mat4 a, b, want, got;
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            a[c][r] = static_cast<float>(1 + 4 * c + r);
            b[c][r] = static_cast<float>(17 + 4 * c + r);
        }
    }
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) {
                sum += a[k][r] * b[c][k];
            }
            want[c][r] = sum;
        }
    }
    glmc_mat4_mul(a, b, got);
    bool same = true;
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            same = same && got[c][r] == want[c][r];
        }
    }
    CHECK(same);
    CHECK(got[3][3] == 4*29 + 8*30 + 12*31 + 16*32);

    mat4s as, bs;
    glm_mat4_copy(a, as.raw);
    glm_mat4_copy(b, bs.raw);
    const mat4s prod = glms_mat4_mul(as, bs);
    CHECK(prod.raw[2][1] == want[2][1] && prod.raw[0][3] == want[0][3]);
}

void testInverse() {
    vec3 t = {1.0f, 2.0f, 3.0f}, s = {2.0f, 4.0f, 8.0f};
    mat4 tm, sm, m, inv;
    glm_translate_make(tm, t);
    glm_scale_make(sm, s);
    glmc_mat4_mul(tm, sm, m);
    glmc_mat4_inv(m, inv);
    const float tol = 1e-6f;
    CHECK(approx(inv[0][0], 0.5f, tol) && approx(inv[1][1], 0.25f, tol) && approx(inv[2][2], 0.125f, tol));
    CHECK(approx(inv[3][0], -0.5f, tol) && approx(inv[3][1], -0.5f, tol) && approx(inv[3][2], -0.375f, tol));
}

void testQuaternion() {
    vec3 z = {0.0f, 0.0f, 1.0f}, x = {1.0f, 0.0f, 0.0f}, out;
    versor q;
    glmc_quatv(q, GLM_PI_2f, z);
    glmc_quat_rotatev(q, x, out);
    const float tol = 1e-6f;
    CHECK(approx(out[0], 0.0f, tol) && approx(out[1], 1.0f, tol) && approx(out[2], 0.0f, tol));

    vec3s zs, xs;
    glm_vec3_copy(z, zs.raw);
    glm_vec3_copy(x, xs.raw);
    const vec3s outs = glms_quat_rotatev(glms_quatv(GLM_PI_2f, zs), xs);
    CHECK(approx(outs.raw[0], 0.0f, tol) && approx(outs.raw[1], 1.0f, tol));
}

void testPerspective() {
    mat4 p;
    glmc_perspective(GLM_PI_2f, 2.0f, 1.0f, 3.0f, p);
    const float tol = 1e-6f;
    CHECK(approx(p[0][0], 0.5f, tol) && approx(p[1][1], 1.0f, tol));
    CHECK(approx(p[2][2], -2.0f, tol) && approx(p[2][3], -1.0f, tol) && approx(p[3][2], -3.0f, tol));

    vec4 farp = {0.0f, 0.0f, -3.0f, 1.0f}, clip;
    glm_mat4_mulv(p, farp, clip);
    CHECK(approx(clip[2] / clip[3], 1.0f, tol));
}

}  // namespace

int main() {
    testMultiply();
    testInverse();
    testQuaternion();
    testPerspective();
    if (failures) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("cglm: ok\n");
    return 0;
}
