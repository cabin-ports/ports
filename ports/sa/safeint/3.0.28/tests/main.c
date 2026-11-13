#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>

/* The hook safe_math_impl.h documents: a consumer that defines
   SAFE_MATH_FAIL_DEFINED supplies safe_math_fail, which every failing safe_*
   function calls.  This one jumps back into the test instead of aborting, and
   is noreturn like upstream's default. */
#define SAFE_MATH_FAIL_DEFINED
static jmp_buf fail_jump;
static int fail_calls;

static _Noreturn void safe_math_fail(const char *msg)
{
    (void)msg;
    ++fail_calls;
    longjmp(fail_jump, 1);
}

#include <safe_math.h>

static int failures;

static void check(int ok, const char *what)
{
    if (!ok) {
        fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

#define EXPECT_FAIL(expr, what)                                                                                        \
    do {                                                                                                               \
        if (setjmp(fail_jump) == 0) {                                                                                  \
            (void)(expr);                                                                                              \
            check(0, what);                                                                                            \
        }                                                                                                              \
    } while (0)

int main(void)
{
    int32_t i32;
    uint32_t u32;
    int64_t i64;
    uint64_t u64;

    check(check_add_int32_int32(INT32_MAX - 1, 1, &i32) && i32 == INT32_MAX, "check_add_int32_int32 at the limit");
    check(!check_add_int32_int32(INT32_MAX, 1, &i32), "check_add_int32_int32 overflow");
    check(check_add_int32_uint32(-1, 1u, &i32) && i32 == 0, "check_add_int32_uint32 mixes signedness");
    check(!check_add_uint32_int32(0u, -1, &u32), "check_add_uint32_int32 below zero");
    check(check_sub_uint32_uint32(5u, 3u, &u32) && u32 == 2u, "check_sub_uint32_uint32 difference");
    check(!check_sub_uint32_uint32(0u, 1u, &u32), "check_sub_uint32_uint32 underflow");
    check(check_mul_uint32_uint32(65535u, 65537u, &u32) && u32 == UINT32_MAX, "check_mul_uint32_uint32 at the limit");
    check(!check_mul_uint32_uint32(65536u, 65536u, &u32), "check_mul_uint32_uint32 overflow");
    check(check_mul_int64_int64(-3037000499LL, 3037000499LL, &i64) && i64 == -9223372030926249001LL,
          "check_mul_int64_int64 near the limit");
    check(!check_mul_int64_int64(INT64_MIN, -1, &i64), "check_mul_int64_int64 overflow");
    check(check_mul_uint64_uint64(0x100000000ULL, 0xFFFFFFFFULL, &u64) && u64 == 0xFFFFFFFF00000000ULL,
          "check_mul_uint64_uint64 product");
    check(!check_mul_uint64_uint64(0x100000000ULL, 0x100000000ULL, &u64), "check_mul_uint64_uint64 overflow");
    check(check_div_int32_int32(-7, 2, &i32) && i32 == -3, "check_div_int32_int32 quotient");
    check(!check_div_int32_int32(INT32_MIN, -1, &i32) && !check_div_int32_int32(1, 0, &i32),
          "check_div_int32_int32 overflow and division by zero");

    check(check_cast_uint8_int32(255) == 0 && check_cast_uint8_int32(256) != 0 && check_cast_uint8_int32(-1) != 0,
          "check_cast_uint8_int32 range");
    check(check_cast_int8_int32(-128) == 0 && check_cast_int8_int32(128) != 0, "check_cast_int8_int32 range");
    check(check_cast_int32_uint64(0x7FFFFFFFULL) == 0 && check_cast_int32_uint64(0x80000000ULL) != 0,
          "check_cast_int32_uint64 range");

    check(safe_add_int32_int32(2, 3) == 5, "safe_add_int32_int32 sum");
    check(safe_sub_int64_int64(-2, 3) == -5, "safe_sub_int64_int64 difference");
    check(safe_mul_int64_int64(-4, 5) == -20, "safe_mul_int64_int64 product");
    check(safe_cast_uint16_int64(65535) == 65535, "safe_cast_uint16_int64 value");
    check(safe_mul_uint_uint(65535u, 65537u) == 4294967295u && check_cast_ushort_int(65536) != 0,
          "safe_math.h's wrappers named for C types");
    EXPECT_FAIL(safe_add_int32_int32(INT32_MAX, 1), "safe_add_int32_int32 overflow fails");
    EXPECT_FAIL(safe_div_uint32_uint32(1u, 0u), "safe_div_uint32_uint32 division by zero fails");
    EXPECT_FAIL(safe_sub_uint64_uint64(0, 1), "safe_sub_uint64_uint64 underflow fails");
    EXPECT_FAIL(safe_cast_uint16_int64(-1), "safe_cast_uint16_int64 negative fails");
    check(fail_calls == 4, "safe_math_fail ran once per failure");

    if (failures != 0) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    puts("safeint 3.0.28 C ok");
    return 0;
}
