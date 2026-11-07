// The C++ consumer: Imath's vector, matrix, quaternion, Euler, frame and
// box templates, and what the library compiles: the half class's table and
// stream functions (half.cpp), HSV conversion (ImathColorAlgo.cpp), float
// successors (ImathFun.cpp), the Jacobi eigensolver and the Procrustes fit
// (ImathMatrixAlgo.cpp), and the random number generators
// (ImathRandom.cpp).
#include <Imath/ImathBox.h>
#include <Imath/ImathColorAlgo.h>
#include <Imath/ImathEuler.h>
#include <Imath/ImathFrame.h>
#include <Imath/ImathFun.h>
#include <Imath/ImathMatrix.h>
#include <Imath/ImathMatrixAlgo.h>
#include <Imath/ImathQuat.h>
#include <Imath/ImathRandom.h>
#include <Imath/half.h>
// The flat include directory Imath.pc's Cflags add.
#include <ImathVec.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>

using namespace Imath;

static_assert(std::is_same<V3f, Imath_3_2::Vec3<float>>::value,
              "Imath exposes the versioned Imath_3_2 namespace");

namespace {

bool ok = true;

void check(bool pass, const char* what) {
    if (!pass) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ok = false;
    }
}

void reports_its_version() {
    check(std::strcmp(IMATH_VERSION_STRING, "3.2.3") == 0, IMATH_VERSION_STRING);
    check(IMATH_VERSION_HEX == 0x03020300u, "IMATH_VERSION_HEX");
    check(std::strcmp(IMATH_LIB_VERSION_STRING, "30.3.2.3") == 0, "the library API version");
}

void halves() {
    half tenth(0.1f);
    check(tenth.bits() == 0x2e66 && float(tenth) == 0.0999755859375f, "half(0.1f)");
    check(float(std::numeric_limits<half>::max()) == 65504.0f, "numeric_limits<half>::max()");
    check(half(65520.0f).isInfinity() && half(-0.0f).isNegative(), "infinity and negative zero");
    check(float(half(1.0f) + half(0.5f)) == 1.5f, "half arithmetic");

    std::ostringstream out;
    out << half(1.5f) << ' ' << half(-0.25f);
    check(out.str() == "1.5 -0.25", "writing halves to a stream");
    half read;
    std::istringstream in("0.333333");
    in >> read;
    check(read.bits() == 0x3555, "reading a half from a stream rounds to nearest");

    char bits[19];
    printBits(bits, half(-2.5f));
    check(std::strcmp(bits, "1 10000 0100000000") == 0, bits);
}

void steps_to_the_next_float() {
    check(succf(1.0f) == 1.0f + FLT_EPSILON, "succf(1)");
    check(predf(1.0f) == 1.0f - FLT_EPSILON / 2, "predf(1)");
    check(succd(0.0) == std::numeric_limits<double>::denorm_min(), "succd(0)");
    check(predd(-DBL_MAX) == -std::numeric_limits<double>::infinity(), "predd(-DBL_MAX)");
    check(succf(std::numeric_limits<float>::infinity()) == std::numeric_limits<float>::infinity(),
          "succf(inf) stays infinite");
}

void converts_colors() {
    check(rgb2hsv(V3f(1, 0, 0)).equalWithAbsError(V3f(0, 1, 1), 1e-6f), "red in HSV");
    check(rgb2hsv(V3d(0, 0.5, 0.5)).equalWithAbsError(V3d(0.5, 1, 0.5), 1e-12), "dark cyan in HSV");
    check(hsv2rgb(V3d(1.0 / 3, 0.5, 0.8)).equalWithAbsError(V3d(0.4, 0.8, 0.4), 1e-12),
          "HSV green to RGB");
    const V3d colors[] = {V3d(0.2, 0.4, 0.6), V3d(0.9, 0.1, 0.5), V3d(0.3, 0.3, 0.3)};
    for (const V3d& rgb : colors) {
        check(hsv2rgb(rgb2hsv(rgb)).equalWithAbsError(rgb, 1e-12), "RGB -> HSV -> RGB");
    }
    const C4f translucent = hsv2rgb(C4f(0.5f, 1, 1, 0.25f));
    check(translucent.r == 0 && translucent.g == 1 && translucent.b == 1 && translucent.a == 0.25f,
          "HSV with alpha keeps the alpha");
}

void transforms() {
    // Imath multiplies row vectors on the left, p * M, so R * T rotates
    // first and then translates.
    M44d rotation, translation;
    rotation.setAxisAngle(V3d(0, 0, 1), M_PI / 2);
    translation.setTranslation(V3d(1, 2, 3));
    const M44d rotate_and_move = rotation * translation;
    const V3d moved = V3d(1, 0, 0) * rotate_and_move;
    check(moved.equalWithAbsError(V3d(1, 3, 3), 1e-12), "rotate a quarter turn, then translate");
    check((rotate_and_move * rotate_and_move.inverse()).equalWithAbsError(M44d(), 1e-12),
          "M * M^-1 is the identity");

    bool threw = false;
    try {
        M44d(0.0).gjInverse(true);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    check(threw, "inverting a singular matrix throws");

    const Eulerd angles(V3d(0.1, -0.2, 0.3), Eulerd::XYZ);
    Eulerd extracted(Eulerd::XYZ);
    extracted.extract(angles.toMatrix44());
    check(extracted.equalWithAbsError(angles, 1e-12), "Euler angles survive a matrix");

    Quatd quarter;
    quarter.setAxisAngle(V3d(0, 0, 1), M_PI / 2);
    const Quatd eighth = slerp(Quatd(), quarter, 0.5);
    check(std::fabs(eighth.angle() - M_PI / 4) < 1e-12 &&
              eighth.axis().equalWithAbsError(V3d(0, 0, 1), 1e-12),
          "slerp halfway to a quarter turn is an eighth turn");

    // The frame's rows: the tangent, the normal, the binormal, and the
    // first point.
    const M44d frame = firstFrame(V3d(1, 2, 3), V3d(3, 2, 3), V3d(1, 5, 3));
    check(frame.equalWithAbsError(M44d(1, 0, 0, 0, 0, 0, 1, 0, 0, -1, 0, 0, 1, 2, 3, 1), 1e-12),
          "the frame of three points");
    // Collinear points leave the normal to be picked across the tangent.
    const M44d line = firstFrame(V3d(0, 0, 0), V3d(0, 0, 2), V3d(0, 0, 5));
    check(line.equalWithAbsError(M44d(0, 0, 1, 0, -1, 0, 0, 0, 0, -1, 0, 0, 0, 0, 0, 1), 1e-12),
          "the frame of collinear points");

    Box3f box(V3f(0), V3f(1));
    check(box.intersects(Box3f(V3f(0.5f), V3f(2))) && !box.intersects(Box3f(V3f(1.5f), V3f(3))),
          "box overlap");
    box.extendBy(V3f(-1, 4, 0.5f));
    check(box.min == V3f(-1, 0, 0) && box.max == V3f(1, 4, 1), "extending a box by a point");
}

void solves_eigensystems() {
    const M33d symmetric(2, 1, 0, 1, 2, 0, 0, 0, 5);
    M33d work = symmetric, vectors;
    V3d values;
    jacobiEigenSolver(work, values, vectors);
    double sorted[3] = {values[0], values[1], values[2]};
    std::sort(sorted, sorted + 3);
    check(std::fabs(sorted[0] - 1) < 1e-12 && std::fabs(sorted[1] - 3) < 1e-12 &&
              std::fabs(sorted[2] - 5) < 1e-12,
          "eigenvalues 1, 3, 5");
    M33d diagonal(values[0], 0, 0, 0, values[1], 0, 0, 0, values[2]);
    check((vectors * diagonal * vectors.transposed()).equalWithAbsError(symmetric, 1e-12),
          "A = V S V^T");

    M33f again(2, 1, 0, 1, 2, 0, 0, 0, 5);
    V3f largest;
    maxEigenVector(again, largest);
    check(std::fabs(std::fabs(largest.z) - 1) < 1e-6f, "the largest eigenvalue's vector is z");
}

void fits_rigid_transforms() {
    M44d rotation, translation;
    rotation.setEulerAngles(V3d(0.3, -0.4, 1.1));
    translation.setTranslation(V3d(-2, 0.5, 4));
    const M44d truth = rotation * translation;
    const V3d from[] = {V3d(0, 0, 0), V3d(1, 0, 0), V3d(0, 2, 0), V3d(0, 0, 3), V3d(1, 1, 1)};
    V3d to[5];
    for (int i = 0; i < 5; i++) {
        to[i] = from[i] * truth;
    }
    const M44d fit = procrustesRotationAndTranslation(from, to, 5);
    check(fit.equalWithAbsError(truth, 1e-9), "Procrustes recovers rotation and translation");
}

// x[n+1] = (0x5deece66d x[n] + 0xb) mod 2^48, as POSIX specifies for
// erand48 and nrand48.
uint64_t lcg(uint64_t x) {
    return (0x5deece66dull * x + 0xb) & ((1ull << 48) - 1);
}

void generates_rand48_sequences() {
    unsigned short state[3] = {0x330e, 0x5678, 0x1234};
    uint64_t x = 0x12345678330eull;
    bool same = true;
    for (int i = 0; i < 1000; i++) {
        x = lcg(x);
        same = same && Imath::nrand48(state) == long(x >> 17);
        x = lcg(x);
        // The significand's 52 bits: the 48 state bits, then their top 4.
        const double expected = std::ldexp(double((x << 4) | (x >> 44)), -52);
        same = same && Imath::erand48(state) == expected;
    }
    check(same, "nrand48 and erand48 follow the rand48 recurrence");

    // Rand32::nextf fills a float's significand with the low 23 bits of a
    // 32-bit LCG; those bits do not depend on the width of unsigned long.
    Rand32 generator(7);
    uint32_t s = (7u * 0xa5a573a5u) ^ 0x5a5a5a5au;
    bool follows = true;
    for (int i = 0; i < 1000; i++) {
        s = 1664525u * s + 1013904223u;
        const uint32_t bits = 0x3f800000u | (s & 0x7fffffu);
        float one_to_two;
        std::memcpy(&one_to_two, &bits, sizeof one_to_two);
        follows = follows && generator.nextf() == one_to_two - 1;
    }
    check(follows, "Rand32::nextf follows its recurrence");
    for (int i = 0; i < 100; i++) {
        check(solidSphereRand<V3f>(generator).length() <= 1.0f, "a point in the unit ball");
    }
}

}  // namespace

int main() {
    reports_its_version();
    halves();
    steps_to_the_next_float();
    converts_colors();
    transforms();
    solves_eigensystems();
    fits_rigid_transforms();
    generates_rand48_sequences();
    std::printf("imath %s: %s\n", IMATH_VERSION_STRING, ok ? "ok" : "FAILED");
    return ok ? 0 : 1;
}
