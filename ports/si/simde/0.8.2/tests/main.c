/* Runs SSE2, SSSE3, SSE4.1, AVX2, SVML and NEON operations through simde
 * and checks each lane against a scalar computation or a known value. */
#include <simde/arm/neon.h>
#include <simde/x86/avx2.h>
#include <simde/x86/svml.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int ok = 1;

static void check(int pass, const char *what)
{
    if (!pass) {
        fprintf(stderr, "FAIL: %s\n", what);
        ok = 0;
    }
}

static int16_t sat16(int32_t v)
{
    return (int16_t) (v > INT16_MAX ? INT16_MAX : v < INT16_MIN ? INT16_MIN : v);
}

static int8_t sat8(int v)
{
    return (int8_t) (v > INT8_MAX ? INT8_MAX : v < INT8_MIN ? INT8_MIN : v);
}

int main(void)
{
    uint8_t u8a[32], u8b[32], u8r[32];
    int8_t i8a[16], i8b[16], i8r[16];
    int16_t i16a[16], i16b[16], i16r[16];
    int32_t i32a[8], i32r[8];
    uint64_t u64r[2];
    int i, pass;
    /* Which paths compiled natively, for reading the CI logs. */
#if defined(SIMDE_X86_SSE2_NATIVE)
    const char *native_sse2 = "yes";
#else
    const char *native_sse2 = "no";
#endif
#if defined(SIMDE_X86_AVX2_NATIVE)
    const char *native_avx2 = "yes";
#else
    const char *native_avx2 = "no";
#endif
#if defined(SIMDE_ARM_NEON_A32V7_NATIVE)
    const char *native_neon = "yes";
#else
    const char *native_neon = "no";
#endif

    for (i = 0; i < 32; ++i) {
        u8a[i] = (uint8_t) (i * 37 + 11);
        u8b[i] = (uint8_t) (255 - i * 23);
    }
    for (i = 0; i < 16; ++i) {
        i8a[i] = (int8_t) (i * 15 - 120);
        i8b[i] = (int8_t) (i * 13 - 100);
        i16a[i] = (int16_t) (i * 4099 - 30000);
        i16b[i] = (int16_t) (20000 - i * 2731);
    }

    /* SSE2: signed saturating add, sum of absolute differences, and the
     * byte sign mask. */
    {
        int16_t x[8] = {32000, -32000, 30000, -30000, 100, -100, INT16_MAX, INT16_MIN};
        int16_t y[8] = {1000, -1000, 2767, -2768, 200, -200, 1, -1};
        simde_mm_storeu_si128(i16r, simde_mm_adds_epi16(simde_mm_loadu_si128(x),
                                                        simde_mm_loadu_si128(y)));
        for (pass = 1, i = 0; i < 8; ++i)
            pass &= i16r[i] == sat16((int32_t) x[i] + y[i]);
        check(pass, "_mm_adds_epi16");
    }
    {
        simde__m128i a = simde_mm_loadu_si128(u8a), b = simde_mm_loadu_si128(u8b);
        uint64_t sad[2] = {0, 0};
        simde_mm_storeu_si128(u64r, simde_mm_sad_epu8(a, b));
        for (i = 0; i < 16; ++i)
            sad[i / 8] += (uint64_t) (u8a[i] > u8b[i] ? u8a[i] - u8b[i] : u8b[i] - u8a[i]);
        check(u64r[0] == sad[0] && u64r[1] == sad[1], "_mm_sad_epu8");
    }
    {
        int mask = 0;
        for (i = 0; i < 16; ++i)
            mask |= (u8a[i] >> 7) << i;
        check(simde_mm_movemask_epi8(simde_mm_loadu_si128(u8a)) == mask, "_mm_movemask_epi8");
    }

    /* SSSE3: a byte shuffle that reads only bit 7 and the low four bits of
     * each index; bit 7 zeroes the lane. */
    {
        uint8_t idx[16];
        for (i = 0; i < 16; ++i)
            idx[i] = (uint8_t) (i % 3 == 0 ? 0x80 + i : (i * 7) % 16 + 16 * (i % 8));
        simde_mm_storeu_si128(u8r, simde_mm_shuffle_epi8(simde_mm_loadu_si128(u8a),
                                                         simde_mm_loadu_si128(idx)));
        for (pass = 1, i = 0; i < 16; ++i)
            pass &= u8r[i] == ((idx[i] & 0x80) ? 0 : u8a[idx[i] & 15]);
        check(pass, "_mm_shuffle_epi8");
    }

    /* SSE4.1: signed 32-bit minimum and a byte blend on the mask's sign. */
    {
        int32_t x[4] = {-5, 7, INT32_MIN, 42}, y[4] = {3, -9, 0, 42};
        simde_mm_storeu_si128(i32r, simde_mm_min_epi32(simde_mm_loadu_si128(x),
                                                       simde_mm_loadu_si128(y)));
        for (pass = 1, i = 0; i < 4; ++i)
            pass &= i32r[i] == (x[i] < y[i] ? x[i] : y[i]);
        check(pass, "_mm_min_epi32");
        simde_mm_storeu_si128(u8r, simde_mm_blendv_epi8(simde_mm_loadu_si128(u8a),
                                                        simde_mm_loadu_si128(u8b),
                                                        simde_mm_loadu_si128(u8a)));
        for (pass = 1, i = 0; i < 16; ++i)
            pass &= u8r[i] == ((u8a[i] & 0x80) ? u8b[i] : u8a[i]);
        check(pass, "_mm_blendv_epi8");
    }

    /* AVX2: 16-bit multiply with pairwise add, and a cross-lane permute
     * that reads the low three bits of each index. */
    {
        simde__m256i a = simde_mm256_loadu_si256(i16a), b = simde_mm256_loadu_si256(i16b);
        int32_t perm[8] = {7, 8, 14, 1, 5, 26, 4, 3};
        simde_mm256_storeu_si256(i32r, simde_mm256_madd_epi16(a, b));
        for (pass = 1, i = 0; i < 8; ++i)
            pass &= i32r[i] == (int32_t) ((int64_t) i16a[2 * i] * i16b[2 * i] +
                                          (int64_t) i16a[2 * i + 1] * i16b[2 * i + 1]);
        check(pass, "_mm256_madd_epi16");
        for (i = 0; i < 8; ++i)
            i32a[i] = i * 1000 - 3500;
        simde_mm256_storeu_si256(i32r, simde_mm256_permutevar8x32_epi32(
                                           simde_mm256_loadu_si256(i32a),
                                           simde_mm256_loadu_si256(perm)));
        for (pass = 1, i = 0; i < 8; ++i)
            pass &= i32r[i] == i32a[perm[i] & 7];
        check(pass, "_mm256_permutevar8x32_epi32");
    }

    /* NEON: saturating add, widening multiply, and a horizontal sum. */
    {
        uint16_t u16r[8];
        int32_t lanes[4] = {123456, -7, 9, -1};
        simde_vst1q_s8(i8r, simde_vqaddq_s8(simde_vld1q_s8(i8a), simde_vld1q_s8(i8b)));
        for (pass = 1, i = 0; i < 16; ++i)
            pass &= i8r[i] == sat8(i8a[i] + i8b[i]);
        check(pass, "vqaddq_s8");
        simde_vst1q_u16(u16r, simde_vmull_u8(simde_vld1_u8(u8a), simde_vld1_u8(u8b)));
        for (pass = 1, i = 0; i < 8; ++i)
            pass &= u16r[i] == (uint16_t) (u8a[i] * u8b[i]);
        check(pass, "vmull_u8");
        check(simde_vaddvq_s32(simde_vld1q_s32(lanes)) == 123457, "vaddvq_s32");
    }

    /* SVML: the emulated exp calls expf, which glibc keeps in libm; the
     * volatile input keeps the compiler from folding it away. */
    {
        volatile float half = 0.5f;
        float in[4], out[4];
        const float want[4] = {0.60653066f, 1.0f, 1.64872127f, 2.71828183f};
        for (i = 0; i < 4; ++i)
            in[i] = half * (float) (i - 1);
        simde_mm_storeu_ps(out, simde_mm_exp_ps(simde_mm_loadu_ps(in)));
        for (pass = 1, i = 0; i < 4; ++i) {
            float d = out[i] - want[i];
            pass &= (d < 0 ? -d : d) <= 1e-6f * want[i];
        }
        check(pass, "_mm_exp_ps");
    }

    printf("simde (C) native paths: SSE2 %s, AVX2 %s, NEON %s\n", native_sse2, native_avx2,
           native_neon);
    printf("simde (C): %s\n", ok ? "ok" : "FAILED");
    return ok ? 0 : 1;
}
