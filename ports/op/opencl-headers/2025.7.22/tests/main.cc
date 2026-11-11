// Checks the OpenCL headers from C++ when targeting OpenCL 1.2: the version
// gating, that entry-point declarations match their *_fn typedefs (inside
// sizeof, so nothing references the ICD loader), that the dispatch table
// keeps every slot when later entry points are hidden, and cl_half.h,
// including negative halves, which C++ converts back without the undefined
// shift C has.
#define CL_TARGET_OPENCL_VERSION 120
#include <CL/cl_function_types.h>
#include <CL/cl_half.h>
#include <CL/cl_icd.h>
#include <CL/opencl.h>

#include <float.h>
#include <stddef.h>
#include <stdio.h>

namespace {

bool ok = true;

void check(bool pass, const char* what) {
  if (!pass) {
    fprintf(stderr, "FAIL: %s\n", what);
    ok = false;
  }
}

// Declared only: deduction fails to compile unless both arguments have the
// same type, and sizeof never evaluates the call.
template <typename T>
char same_type(T, T);

}  // namespace

int main() {
#if !defined(CL_VERSION_1_2) || defined(CL_VERSION_2_0) || \
    !defined(CL_USE_DEPRECATED_OPENCL_1_2_APIS) || defined(CL_USE_DEPRECATED_OPENCL_1_1_APIS)
  check(false, "OpenCL 1.2 version macros");
#endif

  check(sizeof(same_type(&clGetPlatformIDs, clGetPlatformIDs_fn())) == 1 &&
            sizeof(same_type(&clCreateSubDevices, clCreateSubDevices_fn())) == 1 &&
            sizeof(same_type(&clEnqueueNDRangeKernel, clEnqueueNDRangeKernel_fn())) == 1,
        "entry-point types");
  check(offsetof(cl_icd_dispatch, clSetContextDestructorCallback) == 148 * sizeof(void*) &&
            sizeof(cl_icd_dispatch) == 149 * sizeof(void*),
        "dispatch table layout");

  const cl_float4 f4 = {{0.5f, -1.5f, 2.0f, -65504.0f}};
  bool pass = true;
  for (int i = 0; i < 4; ++i)
    pass = pass && cl_half_to_float(cl_half_from_float(f4.s[i], CL_HALF_RTE)) == f4.s[i];
  check(pass, "cl_half round trip");
  check(cl_half_from_float(0.1f, CL_HALF_RTE) == 0x2E66 &&
            cl_half_from_float(0.1f, CL_HALF_RTP) == 0x2E67 &&
            cl_half_from_float(-0.1f, CL_HALF_RTN) == 0xAE67,
        "cl_half_from_float");
  check(cl_half_to_float(0xFC00) < -FLT_MAX && cl_half_to_float(0xBC00) == -1.0f &&
            cl_half_to_float(0x8001) == -1.0f / 16777216,
        "negative cl_half_to_float");

  printf("opencl-headers (C++): %s\n", ok ? "ok" : "FAILED");
  return ok ? 0 : 1;
}
