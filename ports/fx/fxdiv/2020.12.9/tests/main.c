/* Divisors precomputed by fxdiv give the same quotients, remainders and
   rounded-down values as / and % for 32-bit, 64-bit and size_t operands:
   divisors of 1, powers of two, values around 2^32 and 2^63 and each type's
   maximum, against edge-case and pseudo-random numerators. */
#include <fxdiv.h>

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

static int failures = 0;

static void check(int ok, int line, const char* what)
{
    if (!ok)
    {
        fprintf(stderr, "main.c:%d: check failed: %s\n", line, what);
        ++failures;
    }
}

#define CHECK(expr) check((expr) != 0, __LINE__, #expr)

static void mismatch(const char* type, unsigned long long n, unsigned long long d)
{
    if (failures < 10)
    {
        fprintf(stderr, "%s: wrong result for %llu / %llu\n", type, n, d);
    }
    ++failures;
}

/* xorshift64*, so every platform draws the same numbers. */
static uint64_t state = UINT64_C(0x9E3779B97F4A7C15);

static uint64_t next_random(void)
{
    state ^= state >> 12;
    state ^= state << 25;
    state ^= state >> 27;
    return state * UINT64_C(2685821657736338717);
}

/* A nonzero number of random width, so small divisors are common too. */
static uint64_t random_operand(void)
{
    const uint64_t bits = next_random();
    const uint64_t value = bits >> (next_random() & 63);
    return value != 0 ? value : 1;
}

static void expect_uint32(uint32_t n, uint32_t d)
{
    const struct fxdiv_divisor_uint32_t divisor = fxdiv_init_uint32_t(d);
    const struct fxdiv_result_uint32_t result = fxdiv_divide_uint32_t(n, divisor);
    if (fxdiv_quotient_uint32_t(n, divisor) != n / d || fxdiv_remainder_uint32_t(n, divisor) != n % d ||
        result.quotient != n / d || result.remainder != n % d ||
        fxdiv_round_down_uint32_t(n, divisor) != n - n % d)
    {
        mismatch("uint32_t", n, d);
    }
}

static void expect_uint64(uint64_t n, uint64_t d)
{
    const struct fxdiv_divisor_uint64_t divisor = fxdiv_init_uint64_t(d);
    const struct fxdiv_result_uint64_t result = fxdiv_divide_uint64_t(n, divisor);
    if (fxdiv_quotient_uint64_t(n, divisor) != n / d || fxdiv_remainder_uint64_t(n, divisor) != n % d ||
        result.quotient != n / d || result.remainder != n % d ||
        fxdiv_round_down_uint64_t(n, divisor) != n - n % d)
    {
        mismatch("uint64_t", n, d);
    }
}

static void expect_size(size_t n, size_t d)
{
    const struct fxdiv_divisor_size_t divisor = fxdiv_init_size_t(d);
    const struct fxdiv_result_size_t result = fxdiv_divide_size_t(n, divisor);
    if (fxdiv_quotient_size_t(n, divisor) != n / d || fxdiv_remainder_size_t(n, divisor) != n % d ||
        result.quotient != n / d || result.remainder != n % d ||
        fxdiv_round_down_size_t(n, divisor) != n - n % d)
    {
        mismatch("size_t", n, d);
    }
}

/* The high half of a 128-bit product, from 32-bit limbs. */
static uint64_t reference_mulhi(uint64_t a, uint64_t b)
{
    const uint64_t a_lo = a & UINT32_MAX, a_hi = a >> 32;
    const uint64_t b_lo = b & UINT32_MAX, b_hi = b >> 32;
    const uint64_t hi_lo = a_hi * b_lo;
    const uint64_t cross = ((a_lo * b_lo) >> 32) + (hi_lo & UINT32_MAX) + a_lo * b_hi;
    return a_hi * b_hi + (hi_lo >> 32) + (cross >> 32);
}

static const uint64_t edges[] = {
    1, 2, 3, 5, 6, 7, 10, 13, 100, 255, 256, 641, 1000, 65535, 65536, 65537,
    UINT64_C(1000000007), UINT64_C(0x7FFFFFFF), UINT64_C(0x80000000), UINT64_C(0x80000001),
    UINT64_C(0xFFFFFFFE), UINT64_C(0xFFFFFFFF), UINT64_C(0x100000000), UINT64_C(0x100000001),
    UINT64_C(0x1FFFFFFFF), UINT64_C(1000000000000000000), UINT64_C(0xFFFFFFFF00000001),
    UINT64_C(0x7FFFFFFFFFFFFFFF), UINT64_C(0x8000000000000000), UINT64_C(0x8000000000000001),
    UINT64_C(0xFFFFFFFFFFFFFFFE), UINT64_C(0xFFFFFFFFFFFFFFFF),
};

#define EDGE_COUNT (sizeof(edges) / sizeof(edges[0]))

static void edge_cases(void)
{
    size_t i, j;
    for (i = 0; i < EDGE_COUNT; ++i)
    {
        const uint64_t d = edges[i];
        const uint64_t numerators[] = {0, 1, 2, d - 1, d, d + 1, 2 * d - 1, 2 * d, d * d, UINT64_MAX};
        for (j = 0; j < sizeof(numerators) / sizeof(numerators[0]); ++j)
        {
            const uint64_t n = numerators[j];
            expect_uint64(n, d);
            expect_size((size_t) n, (size_t) d != 0 ? (size_t) d : 1);
            if ((uint32_t) d != 0)
            {
                expect_uint32((uint32_t) n, (uint32_t) d);
            }
        }
        for (j = 0; j < EDGE_COUNT; ++j)
        {
            expect_uint64(edges[j], d);
            expect_uint64(edges[j] - 1, d);
        }
    }
}

static void random_cases(void)
{
    int i;
    for (i = 0; i < 200000; ++i)
    {
        const uint64_t n = next_random();
        const uint64_t d = random_operand();
        expect_uint64(n, d);
        expect_uint64(random_operand(), d);
        expect_size((size_t) n, (size_t) d != 0 ? (size_t) d : 1);
        expect_uint32((uint32_t) n, (uint32_t) d != 0 ? (uint32_t) d : 1);
        if (fxdiv_mulhi_uint64_t(n, d) != reference_mulhi(n, d))
        {
            mismatch("fxdiv_mulhi_uint64_t", n, d);
        }
    }
}

static void divisor_fields(void)
{
    /* Granlund and Montgomery's multiplier for 7: m = floor(2^32 * (2^3 - 7) / 7) + 1. */
    const struct fxdiv_divisor_uint32_t seven = fxdiv_init_uint32_t(7);
    const struct fxdiv_divisor_uint64_t seven64 = fxdiv_init_uint64_t(7);
    const struct fxdiv_divisor_uint32_t one = fxdiv_init_uint32_t(1);

    CHECK(seven.value == 7 && seven.m == UINT32_C(0x24924925) && seven.s1 == 1 && seven.s2 == 2);
    CHECK(seven64.value == 7 && seven64.m == UINT64_C(0x2492492492492493) && seven64.s1 == 1 && seven64.s2 == 2);
    CHECK(one.m == 1 && one.s1 == 0 && one.s2 == 0);

    CHECK(fxdiv_mulhi_uint32_t(UINT32_MAX, UINT32_MAX) == UINT32_C(0xFFFFFFFE));
    CHECK(fxdiv_mulhi_uint64_t(UINT64_MAX, UINT64_MAX) == UINT64_C(0xFFFFFFFFFFFFFFFE));
    CHECK(fxdiv_mulhi_uint64_t(UINT64_C(1) << 32, UINT64_C(1) << 32) == 1);
    CHECK(fxdiv_mulhi_size_t(SIZE_MAX, 2) == 1);
}

int main(void)
{
    divisor_fields();
    edge_cases();
    random_cases();
    if (failures != 0)
    {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
#if defined(_MSC_VER) && _MSC_VER >= 1920 && defined(_M_X64) && !defined(__clang__) && !defined(__INTEL_COMPILER)
    printf("built with MSVC x64's __umulh, _BitScanReverse64 and _udiv128 branches (_MSC_VER %d)\n", _MSC_VER);
#endif
    puts("fxdiv C consumer test passed");
    return 0;
}
