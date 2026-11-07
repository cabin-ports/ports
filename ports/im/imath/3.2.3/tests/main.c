/* The C consumer: half.h's C interface.  imath_half_to_float reads the
 * library's lookup table (half.cpp) unless the compiler targets F16C, so
 * every one of the 65536 halves is checked against a bit-level reference,
 * through the function and through the table, along with float-to-half
 * rounding. */
#include <half.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int ok = 1;

static void check(int pass, const char *what) {
    if (!pass) {
        fprintf(stderr, "FAIL: %s\n", what);
        ok = 0;
    }
}

/* IEEE 754 binary16 to binary32 from the bit layout alone. */
static float reference(unsigned h) {
    unsigned exponent = (h >> 10) & 0x1f, mantissa = h & 0x3ff;
    float magnitude;
    if (exponent == 0) {
        magnitude = ldexpf((float)mantissa, -24);
    } else if (exponent == 31) {
        magnitude = mantissa != 0 ? NAN : INFINITY;
    } else {
        magnitude = ldexpf((float)(mantissa | 0x400), (int)exponent - 25);
    }
    return (h & 0x8000) ? -magnitude : magnitude;
}

static int same(float a, float b) {
    return isnan(a) ? isnan(b) : a == b && !signbit(a) == !signbit(b);
}

static void converts_every_half(void) {
    long wrong = 0, wrong_table = 0, no_round_trip = 0, nans = 0;
    unsigned h;
    for (h = 0; h <= 0xffff; h++) {
        float expected = reference(h);
        float f = imath_half_to_float((imath_half_bits_t)h);
        if (!same(f, expected)) {
            wrong++;
        }
        if (!same(imath_half_to_float_table[h].f, expected)) {
            wrong_table++;
        }
        if (isnan(expected)) {
            nans++;
        } else if (imath_float_to_half(f) != h) {
            no_round_trip++;
        }
    }
    printf("half: %ld wrong, %ld wrong in the table, %ld without a round trip, %ld NaNs\n",
           wrong, wrong_table, no_round_trip, nans);
    check(wrong == 0, "imath_half_to_float matches the bit layout");
    check(wrong_table == 0, "the library's table matches the bit layout");
    check(no_round_trip == 0, "every non-NaN half survives a round trip through float");
    check(nans == 2 * 1023, "2046 halves are NaNs");
}

static void rounds_to_nearest_even(void) {
    half nan_bits = imath_float_to_half(NAN);
    check(imath_float_to_half(1.0f) == 0x3c00, "1");
    check(imath_float_to_half(-2.5f) == 0xc100, "-2.5");
    check(imath_float_to_half(0.1f) == 0x2e66, "0.1 rounds to 0x2e66");
    /* Ties go to the even significand. */
    check(imath_float_to_half(1.0f + ldexpf(1.0f, -11)) == 0x3c00, "1 + 2^-11 ties down");
    check(imath_float_to_half(1.0f + ldexpf(3.0f, -11)) == 0x3c02, "1 + 3 * 2^-11 ties up");
    check(imath_float_to_half(65519.0f) == 0x7bff, "65519 rounds to the largest half");
    check(imath_float_to_half(65520.0f) == 0x7c00, "65520 overflows to infinity");
    check(imath_float_to_half(ldexpf(1.0f, -24)) == 0x0001, "the smallest denormal");
    check(imath_float_to_half(ldexpf(1.0f, -25)) == 0x0000, "half the smallest denormal ties to 0");
    check(imath_float_to_half(ldexpf(3.0f, -26)) == 0x0001, "0.75 of the smallest denormal");
    check(imath_float_to_half(-0.0f) == 0x8000, "negative zero keeps its sign");
    check(imath_float_to_half(-INFINITY) == 0xfc00, "negative infinity");
    check((nan_bits & 0x7c00) == 0x7c00 && (nan_bits & 0x3ff) != 0, "NaN stays NaN");
    check(imath_half_to_float(0x7bff) == HALF_MAX, "HALF_MAX is the largest half");
}

/* ImathConfig.h, which half.h includes: the version macros C can use. */
static void reports_its_version(void) {
    check(IMATH_VERSION_MAJOR == 3 && IMATH_VERSION_MINOR == 2 && IMATH_VERSION_PATCH == 3,
          "version 3.2.3");
    check(strcmp(IMATH_VERSION_STRING, "3.2.3") == 0, IMATH_VERSION_STRING);
}

int main(void) {
    reports_its_version();
    converts_every_half();
    rounds_to_nearest_even();
    return ok ? 0 : 1;
}
