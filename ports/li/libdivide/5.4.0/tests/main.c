/* Opt into the vector API the host has, as the README shows. */
#if defined(__x86_64__) || defined(_M_X64)
#define LIBDIVIDE_SSE2
#elif defined(__aarch64__)
#define LIBDIVIDE_NEON
#endif
#include <libdivide.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int failures = 0;

static uint64_t rng_state = UINT64_C(0x9E3779B97F4A7C15);

/* xorshift64*, shifted down by a random amount so small magnitudes occur. */
static uint64_t next_random(void) {
    uint64_t x;
    rng_state ^= rng_state >> 12;
    rng_state ^= rng_state << 25;
    rng_state ^= rng_state >> 27;
    x = rng_state * UINT64_C(2685821657736338717);
    return x >> (x & 63);
}

static const int64_t edges[] = {
    0, 1, 2, 3, 5, 7, 10, 641, 1000003, -1, -2, -3, -7, -641, INT64_MAX, INT64_MIN,
    INT64_MAX - 1, INT64_MIN + 1, INT32_MAX, INT32_MIN, UINT32_MAX, INT16_MAX, INT16_MIN,
    UINT16_MAX, (int64_t)1 << 31, (int64_t)1 << 32, (int64_t)1 << 40};
#define EDGE_COUNT (sizeof edges / sizeof edges[0])
#define RANDOM_NUMERATORS 200
#define RANDOM_DIVISORS 200

/* Checks the branchfull and branchfree dividers for d against the compiler's
 * own division, over the edge values and random numerators, and checks that
 * both recover d.  MIN / -1 has no representable quotient and is skipped; an
 * unsigned branchfree divider rejects d == 1. */
#define DEFINE_CHECK(T, SFX, MIN, IS_SIGNED, FMT, CAST)                                     \
    static void check_##SFX(T d) {                                                          \
        struct libdivide_##SFX##_t fast = libdivide_##SFX##_gen(d);                         \
        int branchfree = IS_SIGNED || d != 1;                                               \
        struct libdivide_##SFX##_branchfree_t bf;                                           \
        size_t i;                                                                           \
        memset(&bf, 0, sizeof bf);                                                          \
        if (branchfree) bf = libdivide_##SFX##_branchfree_gen(d);                           \
        if (libdivide_##SFX##_recover(&fast) != d ||                                        \
            (branchfree && libdivide_##SFX##_branchfree_recover(&bf) != d)) {               \
            printf(#SFX " recover(" FMT ") failed\n", (CAST)d);                             \
            ++failures;                                                                     \
        }                                                                                   \
        for (i = 0; i < EDGE_COUNT + RANDOM_NUMERATORS; ++i) {                              \
            T n = (T)(i < EDGE_COUNT ? (uint64_t)edges[i] : next_random());                 \
            T want;                                                                         \
            if (IS_SIGNED && n == (T)(MIN) && d == (T)-1) continue;                         \
            want = (T)(n / d);                                                              \
            if (libdivide_##SFX##_do(n, &fast) != want) {                                   \
                printf(#SFX "_do(" FMT ", " FMT ") failed\n", (CAST)n, (CAST)d);            \
                ++failures;                                                                 \
            }                                                                               \
            if (branchfree && libdivide_##SFX##_branchfree_do(n, &bf) != want) {            \
                printf(#SFX "_branchfree_do(" FMT ", " FMT ") failed\n", (CAST)n, (CAST)d); \
                ++failures;                                                                 \
            }                                                                               \
        }                                                                                   \
    }

DEFINE_CHECK(int16_t, s16, INT16_MIN, 1, "%lld", long long)
DEFINE_CHECK(uint16_t, u16, 0, 0, "%llu", unsigned long long)
DEFINE_CHECK(int32_t, s32, INT32_MIN, 1, "%lld", long long)
DEFINE_CHECK(uint32_t, u32, 0, 0, "%llu", unsigned long long)
DEFINE_CHECK(int64_t, s64, INT64_MIN, 1, "%lld", long long)
DEFINE_CHECK(uint64_t, u64, 0, 0, "%llu", unsigned long long)

static void check_divisor(uint64_t bits) {
    if ((uint16_t)bits != 0) {
        check_s16((int16_t)bits);
        check_u16((uint16_t)bits);
    }
    if ((uint32_t)bits != 0) {
        check_s32((int32_t)bits);
        check_u32((uint32_t)bits);
    }
    if (bits != 0) {
        check_s64((int64_t)bits);
        check_u64(bits);
    }
}

#if defined(LIBDIVIDE_SSE2) || defined(LIBDIVIDE_NEON)
/* One vector of each lane width through the branchfull and branchfree
 * vec128 entry points. */
static void check_vectors(void) {
    int32_t s32_in[4] = {INT32_MIN + 1, -100, 99, INT32_MAX};
    int32_t s32_out[4];
    uint64_t u64_in[2] = {UINT64_MAX, UINT64_C(1000000007)};
    uint64_t u64_out[2];
    struct libdivide_s32_t s32_d = libdivide_s32_gen(-7);
    struct libdivide_u64_branchfree_t u64_d = libdivide_u64_branchfree_gen(UINT64_C(12345));
    int i;
#if defined(LIBDIVIDE_SSE2)
    __m128i v = _mm_loadu_si128((const __m128i *)s32_in);
    _mm_storeu_si128((__m128i *)s32_out, libdivide_s32_do_vec128(v, &s32_d));
    v = _mm_loadu_si128((const __m128i *)u64_in);
    _mm_storeu_si128((__m128i *)u64_out, libdivide_u64_branchfree_do_vec128(v, &u64_d));
#else
    vst1q_s32(s32_out, libdivide_s32_do_vec128(vld1q_s32(s32_in), &s32_d));
    vst1q_u64(u64_out, libdivide_u64_branchfree_do_vec128(vld1q_u64(u64_in), &u64_d));
#endif
    for (i = 0; i < 4; ++i) {
        if (s32_out[i] != s32_in[i] / -7) {
            printf("s32_do_vec128 lane %d failed\n", i);
            ++failures;
        }
    }
    for (i = 0; i < 2; ++i) {
        if (u64_out[i] != u64_in[i] / 12345) {
            printf("u64_branchfree_do_vec128 lane %d failed\n", i);
            ++failures;
        }
    }
}
#endif

int main(void) {
    size_t i;
    int64_t sum = 0;
    int64_t numbers[100];
    struct libdivide_s64_t d;

    if (strcmp(LIBDIVIDE_VERSION, "5.4.0") != 0 || LIBDIVIDE_VERSION_MAJOR != 5 ||
        LIBDIVIDE_VERSION_MINOR != 4 || LIBDIVIDE_VERSION_PATCH != 0) {
        printf("unexpected version %s\n", LIBDIVIDE_VERSION);
        ++failures;
    }

    for (i = 0; i < EDGE_COUNT; ++i) check_divisor((uint64_t)edges[i]);
    for (i = 0; i < RANDOM_DIVISORS; ++i) check_divisor(next_random());

    /* The README's example: one divider reused across an array. */
    for (i = 0; i < 100; ++i) numbers[i] = (int64_t)(i * i) * 7919 - 40000;
    d = libdivide_s64_gen(-37);
    for (i = 0; i < 100; ++i) sum += libdivide_s64_do(numbers[i], &d) - numbers[i] / -37;
    if (sum != 0) {
        printf("array division drifted by %lld\n", (long long)sum);
        ++failures;
    }

#if defined(LIBDIVIDE_SSE2) || defined(LIBDIVIDE_NEON)
    check_vectors();
#endif

    if (failures != 0) {
        printf("libdivide: %d failures\n", failures);
        return 1;
    }
    printf("libdivide: ok\n");
    return 0;
}
