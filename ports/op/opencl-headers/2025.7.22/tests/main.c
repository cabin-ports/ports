/* Checks the OpenCL headers without an ICD loader, so no cl* entry point is
 * called or referenced: the default target version, scalar and vector
 * types, API constants, the version macros, the ICD dispatch table's layout
 * and function types, and cl_half.h's conversions in every rounding mode.
 * CL_TARGET_OPENCL_VERSION is left unset to take cl_version.h's default. */
#include <CL/cl_half.h>
#include <CL/cl_icd.h>
#include <CL/opencl.h>

#include <float.h>
#include <stddef.h>
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

static cl_int CL_API_CALL fake_get_platform_ids(cl_uint num_entries,
                                                cl_platform_id *platforms,
                                                cl_uint *num_platforms)
{
    (void) platforms;
    *num_platforms = num_entries + 2;
    return CL_INVALID_VALUE;
}

struct half_case {
    float f;
    cl_half_rounding_mode mode;
    cl_half want;
};

int main(void)
{
    /* IEEE binary16 encodings: 0.7 and 1/3 are inexact, 1 + 2^-11 is a tie,
     * 2 - 2^-12 carries into the exponent, 70000 overflows and 2^-25 is half
     * the smallest subnormal. */
    static const struct half_case cases[] = {
        {0.7f, CL_HALF_RTE, 0x399A},          {0.7f, CL_HALF_RTZ, 0x3999},
        {0.7f, CL_HALF_RTP, 0x399A},          {0.7f, CL_HALF_RTN, 0x3999},
        {-1.0f / 3, CL_HALF_RTE, 0xB555},     {-1.0f / 3, CL_HALF_RTZ, 0xB555},
        {-1.0f / 3, CL_HALF_RTP, 0xB555},     {-1.0f / 3, CL_HALF_RTN, 0xB556},
        {1.0f + 1.0f / 2048, CL_HALF_RTE, 0x3C00},
        {1.0f + 3.0f / 2048, CL_HALF_RTE, 0x3C02},
        {2.0f - 1.0f / 4096, CL_HALF_RTE, 0x4000}, {2.0f - 1.0f / 4096, CL_HALF_RTZ, 0x3FFF},
        {70000.0f, CL_HALF_RTE, 0x7C00},      {70000.0f, CL_HALF_RTZ, 0x7BFF},
        {-70000.0f, CL_HALF_RTP, 0xFBFF},     {-70000.0f, CL_HALF_RTN, 0xFC00},
        {1.0f / 33554432, CL_HALF_RTE, 0x0000}, {1.0f / 33554432, CL_HALF_RTP, 0x0001},
    };
    size_t i;
    int pass;
    cl_uint n = 0;
    cl_icd_dispatch dispatch;
    cl_int4 v = {{1, -2, 3, -4}};
    cl_float4 f4 = {{0.5f, 1.5f, -2.0f, 4.0f}};
    const cl_version version = CL_MAKE_VERSION(3, 2, 17);

    check(CL_TARGET_OPENCL_VERSION == 300, "default CL_TARGET_OPENCL_VERSION");
#if !defined(CL_VERSION_3_0) || defined(CL_USE_DEPRECATED_OPENCL_2_2_APIS)
    check(0, "OpenCL 3.0 version macros");
#endif

    check(sizeof(cl_char) == 1 && sizeof(cl_short) == 2 && sizeof(cl_int) == 4 &&
              sizeof(cl_long) == 8 && sizeof(cl_half) == 2 && sizeof(cl_float) == 4 &&
              sizeof(cl_double) == 8,
          "scalar sizes");
    check((cl_char) -1 < 0 && (cl_uint) -1 == CL_UINT_MAX && CL_INT_MAX == 2147483647 &&
              CL_INT_MIN == -CL_INT_MAX - 1,
          "scalar ranges");
    check(sizeof(cl_int4) == 16 && sizeof(cl_float4) == 16 && sizeof(cl_double16) == 128,
          "vector sizes");
    check(v.s[1] == -2 && v.s[3] == -4 && f4.s[2] == -2.0f, "vector elements");
#if __CL_HAS_ANON_STRUCT__
    check(v.x == 1 && v.w == -4 && v.s2 == 3 && v.lo.s[1] == -2 && v.hi.s[0] == 3,
          "vector named members");
#endif

    /* Values the OpenCL specification fixes. */
    check(CL_SUCCESS == 0 && CL_DEVICE_NOT_FOUND == -1 && CL_BUILD_PROGRAM_FAILURE == -11 &&
              CL_INVALID_VALUE == -30 && CL_INVALID_KERNEL_ARGS == -52,
          "error codes");
    check(CL_DEVICE_TYPE_CPU == 2 && CL_DEVICE_TYPE_GPU == 4 &&
              CL_DEVICE_TYPE_ALL == 0xFFFFFFFF && CL_MEM_READ_WRITE == 1 &&
              CL_MEM_COPY_HOST_PTR == 32,
          "bitfield values");
    check(CL_PLATFORM_PROFILE == 0x0900 && CL_PLATFORM_NAME == 0x0902 &&
              CL_DEVICE_TYPE == 0x1000 && CL_DEVICE_NUMERIC_VERSION == 0x105E &&
              CL_DEVICE_HALF_FP_CONFIG == 0x1033 && CL_GL_OBJECT_BUFFER == 0x2000,
          "query names");
    check(strcmp(CL_KHR_FP16_EXTENSION_NAME, "cl_khr_fp16") == 0, "extension name");
    check(version == ((3u << 22) | (2u << 12) | 17u) && CL_VERSION_MAJOR(version) == 3 &&
              CL_VERSION_MINOR(version) == 2 && CL_VERSION_PATCH(version) == 17,
          "CL_MAKE_VERSION");

    /* The dispatch table's slots are ABI shared by loaders and drivers. */
    check(offsetof(cl_icd_dispatch, clGetPlatformIDs) == 0 &&
              offsetof(cl_icd_dispatch, clCreateContext) == 4 * sizeof(void *) &&
              offsetof(cl_icd_dispatch, clCreateBuffer) == 14 * sizeof(void *) &&
              offsetof(cl_icd_dispatch, clSetContextDestructorCallback) == 148 * sizeof(void *) &&
              sizeof(cl_icd_dispatch) == 149 * sizeof(void *),
          "dispatch table layout");
    memset(&dispatch, 0, sizeof dispatch);
    dispatch.clGetPlatformIDs = fake_get_platform_ids;
    check(dispatch.clGetPlatformIDs(5, NULL, &n) == CL_INVALID_VALUE && n == 7,
          "dispatch through clGetPlatformIDs_t");

    for (pass = 1, i = 0; i < sizeof cases / sizeof cases[0]; ++i)
        pass &= cl_half_from_float(cases[i].f, cases[i].mode) == cases[i].want;
    check(pass, "cl_half_from_float");
    check(cl_half_from_double(1.0 / 3, CL_HALF_RTP) == 0x3556 &&
              cl_half_from_double(-70000.0, CL_HALF_RTZ) == 0xFBFF,
          "cl_half_from_double");
    check(cl_half_to_float(0x3555) == 0.333251953125f &&
              cl_half_to_float(0x0001) == 1.0f / 16777216 && cl_half_to_float(0x7BFF) == CL_HALF_MAX &&
              cl_half_to_float(0x7C00) > FLT_MAX && cl_half_to_float(0x7E00) != cl_half_to_float(0x7E00),
          "cl_half_to_float");

    printf("opencl-headers (C): %s\n", ok ? "ok" : "FAILED");
    return ok ? 0 : 1;
}
