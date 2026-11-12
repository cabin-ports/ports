// Links the C loader from C++, where cl.h's declarations must keep C linkage
// to resolve: it forwards calls through a fake ICD object and, off Windows,
// enumerates no platforms with nowhere to find vendors (main.c explains why
// Windows skips that).
#include <CL/cl_icd.h>
#include <stdio.h>
#include <stdlib.h>

namespace {

bool ok = true;

void check(bool cond, const char *what)
{
    if (!cond) {
        printf("FAIL: %s\n", what);
        ok = false;
    }
}

struct fake_object {
    cl_icd_dispatch *dispatch;
};

cl_icd_dispatch table;
fake_object device = {&table};
const void *seen_device;

}  // namespace

extern "C" {
static cl_int CL_API_CALL fake_get_device_info(cl_device_id id, cl_device_info name,
                                               size_t size, void *value, size_t *size_ret)
{
    seen_device = id;
    if (name != CL_DEVICE_MAX_COMPUTE_UNITS || size != sizeof(cl_uint)) {
        return CL_INVALID_VALUE;
    }
    *static_cast<cl_uint *>(value) = 12;
    if (size_ret != NULL) {
        *size_ret = sizeof(cl_uint);
    }
    return CL_SUCCESS;
}

static cl_int CL_API_CALL fake_release_device(cl_device_id id)
{
    seen_device = id;
    return -1069;
}
}

int main()
{
#if !defined(_WIN32)
    // Neither path can be a directory, so no vendor or layer is found; stop
    // before anything initializes the loader if they cannot be set.
    if (setenv("OCL_ICD_VENDORS", "/dev/null/vendors", 1) != 0 ||
        setenv("OPENCL_LAYER_PATH", "/dev/null/layers", 1) != 0 ||
        unsetenv("OCL_ICD_FILENAMES") != 0 || unsetenv("OPENCL_LAYERS") != 0) {
        printf("FAIL: environment\nopencl-icd-loader (C++): FAILED\n");
        return 1;
    }
#endif

    table.clGetDeviceInfo = fake_get_device_info;
    table.clReleaseDevice = fake_release_device;
    cl_device_id id = reinterpret_cast<cl_device_id>(&device);

    cl_uint units = 0;
    size_t size = 0;
    check(clGetDeviceInfo(id, CL_DEVICE_MAX_COMPUTE_UNITS, sizeof units, &units, &size) ==
                  CL_SUCCESS &&
              units == 12 && size == sizeof units && seen_device == &device,
          "clGetDeviceInfo");
    seen_device = NULL;
    check(clReleaseDevice(id) == -1069 && seen_device == &device, "clReleaseDevice");
    check(clGetDeviceInfo(NULL, CL_DEVICE_MAX_COMPUTE_UNITS, sizeof units, &units, NULL) ==
              CL_INVALID_DEVICE,
          "NULL device");

#if !defined(_WIN32)
    cl_uint count = 3;
    check(clGetPlatformIDs(0, NULL, &count) == CL_PLATFORM_NOT_FOUND_KHR && count == 0,
          "clGetPlatformIDs");
#endif

    printf("opencl-icd-loader (C++): %s\n", ok ? "ok" : "FAILED");
    return ok ? 0 : 1;
}
