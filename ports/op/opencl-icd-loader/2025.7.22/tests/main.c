// Exercises the static loader without loading an ICD.  Fake ICD objects, one
// classic and one cl_khr_icd2 tagged, check that the loader forwards calls
// through the right dispatch slot with their arguments and results, and that
// it rejects NULL handles and missing extension entry points.  Off Windows the
// loader then initializes with nowhere to find vendors or layers, and the test
// checks platform enumeration and the loader's own extension functions.  The
// Windows loader always searches the registry, display adapters and the
// OpenCL compatibility pack, which nothing can redirect, so there the test
// never initializes it.
#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif
// clGetExtensionFunctionAddress, deprecated since OpenCL 1.1, is how a
// program reaches the loader's own clGetICDLoaderInfoOCLICD.
#define CL_USE_DEPRECATED_OPENCL_1_1_APIS

#include <CL/cl_icd.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int ok = 1;

static void check(int cond, const char *what)
{
    if (!cond) {
        printf("FAIL: %s\n", what);
        ok = 0;
    }
}

// The loader reads an object's dispatch table and, when that table's first
// entry is CL_ICD2_TAG_KHR, the loader-owned table stored after it.
struct fake_object {
    cl_icd_dispatch *dispatch;
    cl_icd_dispatch *disp_data;
};

static cl_icd_dispatch classic;
static cl_icd_dispatch icd2_tag;
static struct fake_object platform = {&classic, NULL};
static struct fake_object context = {&classic, NULL};
static struct fake_object queue = {&classic, NULL};
static struct fake_object kernel = {&classic, NULL};
static struct fake_object event = {&classic, NULL};
static struct fake_object mem = {&classic, NULL};
static struct fake_object device = {&classic, NULL};
static struct fake_object icd2_context = {&icd2_tag, &classic};

static const void *seen_handle;
static const void *seen_arg;
static int user_data;

static cl_int CL_API_CALL fake_get_platform_info(cl_platform_id id, cl_platform_info name,
                                                 size_t size, void *value, size_t *size_ret)
{
    static const char vendor[] = "Cabin fake ICD";
    seen_handle = id;
    if (name != CL_PLATFORM_VENDOR) {
        return CL_INVALID_VALUE;
    }
    if (value != NULL) {
        if (size < sizeof vendor) {
            return CL_INVALID_VALUE;
        }
        memcpy(value, vendor, sizeof vendor);
    }
    if (size_ret != NULL) {
        *size_ret = sizeof vendor;
    }
    return CL_SUCCESS;
}

static cl_int CL_API_CALL fake_retain_context(cl_context id)
{
    seen_handle = id;
    return -1001;
}

// Reached only by a loader that ignores the cl_khr_icd2 tag.
static cl_int CL_API_CALL tagged_retain_context(cl_context id)
{
    seen_handle = id;
    return -1002;
}

static cl_mem CL_API_CALL fake_create_buffer(cl_context id, cl_mem_flags flags, size_t size,
                                             void *host_ptr, cl_int *errcode_ret)
{
    seen_handle = id;
    seen_arg = host_ptr;
    *errcode_ret = flags == CL_MEM_READ_ONLY && size == 4096 ? -1014 : CL_INVALID_VALUE;
    return (cl_mem)(void *)&mem;
}

static cl_int CL_API_CALL fake_enqueue_nd_range_kernel(
    cl_command_queue id, cl_kernel k, cl_uint work_dim, const size_t *offset,
    const size_t *global, const size_t *local, cl_uint num_events, const cl_event *events,
    cl_event *out)
{
    seen_handle = id;
    seen_arg = k;
    if (work_dim != 2 || offset != NULL || global[0] != 64 || global[1] != 32 || local[0] != 8 ||
        local[1] != 4 || num_events != 1 || events[0] != (cl_event)(void *)&event) {
        return CL_INVALID_VALUE;
    }
    *out = (cl_event)(void *)&event;
    return -1122;
}

static void CL_CALLBACK destructor_notify(cl_context id, void *data)
{
    (void)id;
    (void)data;
}

static cl_int CL_API_CALL fake_set_context_destructor_callback(
    cl_context id, void(CL_CALLBACK *notify)(cl_context, void *), void *data)
{
    seen_handle = id;
    seen_arg = data;
    return notify == destructor_notify ? -1148 : CL_INVALID_VALUE;
}

static cl_int CL_API_CALL fake_retain_device_ext(cl_device_id id)
{
    seen_handle = id;
    return -1080;
}

static void *CL_API_CALL fake_get_extension_function_address_for_platform(cl_platform_id id,
                                                                          const char *name)
{
    seen_handle = id;
    return strcmp(name, "clCabinProbeFake") == 0 ? (void *)&user_data : NULL;
}

static cl_context CL_API_CALL fake_create_context_from_type(
    const cl_context_properties *properties, cl_device_type type,
    void(CL_CALLBACK *notify)(const char *, const void *, size_t, void *), void *data,
    cl_int *errcode_ret)
{
    seen_arg = properties;
    *errcode_ret = type == CL_DEVICE_TYPE_GPU && notify == NULL && data == &user_data
                       ? -1005
                       : CL_INVALID_VALUE;
    return (cl_context)(void *)&context;
}

static void check_dispatch(void)
{
    char vendor[32];
    size_t size = 0;
    cl_int err = 0;
    size_t global[2] = {64, 32};
    size_t local[2] = {8, 4};
    cl_event wait = (cl_event)(void *)&event;
    cl_event out = NULL;
    cl_mem buffer;

    check(clGetPlatformInfo((cl_platform_id)(void *)&platform, CL_PLATFORM_VENDOR, sizeof vendor,
                            vendor, &size) == CL_SUCCESS &&
              seen_handle == &platform && size == 15 && strcmp(vendor, "Cabin fake ICD") == 0,
          "clGetPlatformInfo");
    check(clRetainContext((cl_context)(void *)&context) == -1001 && seen_handle == &context,
          "clRetainContext");
    buffer = clCreateBuffer((cl_context)(void *)&context, CL_MEM_READ_ONLY, 4096, &user_data, &err);
    check(buffer == (cl_mem)(void *)&mem && err == -1014 && seen_handle == &context &&
              seen_arg == &user_data,
          "clCreateBuffer");
    check(clEnqueueNDRangeKernel((cl_command_queue)(void *)&queue, (cl_kernel)(void *)&kernel, 2,
                                 NULL, global, local, 1, &wait, &out) == -1122 &&
              seen_handle == &queue && seen_arg == &kernel && out == wait,
          "clEnqueueNDRangeKernel");
    // The last slot of the 149-entry table.
    check(clSetContextDestructorCallback((cl_context)(void *)&context, destructor_notify,
                                         &user_data) == -1148 &&
              seen_handle == &context && seen_arg == &user_data,
          "clSetContextDestructorCallback");

    seen_handle = NULL;
    check(clRetainContext((cl_context)(void *)&icd2_context) == -1001 &&
              seen_handle == &icd2_context,
          "cl_khr_icd2 loader-managed dispatch");

    check(clGetPlatformInfo(NULL, CL_PLATFORM_VENDOR, 0, NULL, &size) == CL_INVALID_PLATFORM,
          "NULL platform");
    check(clRetainContext(NULL) == CL_INVALID_CONTEXT, "NULL context");
    err = 0;
    check(clCreateBuffer(NULL, CL_MEM_READ_ONLY, 4096, NULL, &err) == NULL &&
              err == CL_INVALID_CONTEXT,
          "NULL context for clCreateBuffer");
    check(clEnqueueNDRangeKernel(NULL, (cl_kernel)(void *)&kernel, 2, NULL, global, local, 0, NULL,
                                 NULL) == CL_INVALID_COMMAND_QUEUE,
          "NULL command queue");

    check(clRetainDeviceEXT((cl_device_id)(void *)&device) == CL_INVALID_OPERATION,
          "missing extension entry point");
    classic.clRetainDeviceEXT = fake_retain_device_ext;
    check(clRetainDeviceEXT((cl_device_id)(void *)&device) == -1080 && seen_handle == &device,
          "clRetainDeviceEXT");
}

#if !defined(_WIN32)
static int same_string(clGetICDLoaderInfoOCLICD_fn info, cl_icdl_info name, const char *want)
{
    char value[64];
    size_t size = 0;
    return info(name, sizeof value, value, &size) == CL_SUCCESS && size == strlen(want) + 1 &&
           strcmp(value, want) == 0;
}

static void check_initialized(void)
{
    cl_uint count = 5;
    cl_platform_id ids[1];
    void *address;
    clGetICDLoaderInfoOCLICD_fn info = NULL;
    char small[4];
    size_t size = 0;
    cl_int err = 0;
    cl_context_properties properties[3];

    check(clGetPlatformIDs(0, NULL, NULL) == CL_INVALID_VALUE, "clGetPlatformIDs without outputs");
    check(clGetPlatformIDs(0, NULL, &count) == CL_PLATFORM_NOT_FOUND_KHR && count == 0,
          "clGetPlatformIDs count");
    ids[0] = (cl_platform_id)(void *)&platform;
    check(clGetPlatformIDs(1, ids, NULL) == CL_PLATFORM_NOT_FOUND_KHR && ids[0] == NULL,
          "clGetPlatformIDs list");

    address = clGetExtensionFunctionAddress("clGetICDLoaderInfoOCLICD");
    check(address != NULL, "clGetICDLoaderInfoOCLICD address");
    if (address != NULL) {
        memcpy(&info, &address, sizeof info);
        check(same_string(info, CL_ICDL_VERSION, "3.0.7"), "loader version");
        check(same_string(info, CL_ICDL_OCL_VERSION, "OpenCL 3.0"), "loader OpenCL version");
        check(same_string(info, CL_ICDL_NAME, "Khronos OpenCL ICD Loader"), "loader name");
        check(same_string(info, CL_ICDL_VENDOR, "Khronos Group"), "loader vendor");
        check(info(CL_ICDL_VERSION, sizeof small, small, &size) == CL_INVALID_VALUE &&
                  info(0, 0, NULL, &size) == CL_INVALID_VALUE,
              "loader info errors");
    }
    check(clGetExtensionFunctionAddress("clCreateFromGLBuffer") ==
              (void *)(uintptr_t)&clCreateFromGLBuffer,
          "loader-handled extension function");
    check(clGetExtensionFunctionAddress("clCabinProbeFake") == NULL, "unknown extension function");
    check(clGetExtensionFunctionAddressForPlatform((cl_platform_id)(void *)&platform,
                                                   "clCabinProbeFake") == (void *)&user_data &&
              seen_handle == &platform,
          "clGetExtensionFunctionAddressForPlatform");

    check(clCreateContextFromType(NULL, CL_DEVICE_TYPE_GPU, NULL, NULL, &err) == NULL &&
              err == CL_INVALID_PLATFORM,
          "clCreateContextFromType without a platform");
    properties[0] = CL_CONTEXT_PLATFORM;
    properties[1] = (cl_context_properties)(void *)&platform;
    properties[2] = 0;
    check(clCreateContextFromType(properties, CL_DEVICE_TYPE_GPU, NULL, &user_data, &err) ==
                  (cl_context)(void *)&context &&
              err == -1005 && seen_arg == properties,
          "clCreateContextFromType through CL_CONTEXT_PLATFORM");
}
#endif

int main(void)
{
#if !defined(_WIN32)
    // Neither path can be a directory, so no vendor or layer is found; stop
    // before anything initializes the loader if they cannot be set.
    if (setenv("OCL_ICD_VENDORS", "/dev/null/vendors", 1) != 0 ||
        setenv("OPENCL_LAYER_PATH", "/dev/null/layers", 1) != 0 ||
        unsetenv("OCL_ICD_FILENAMES") != 0 || unsetenv("OPENCL_LAYERS") != 0) {
        printf("FAIL: environment\nopencl-icd-loader (C): FAILED\n");
        return 1;
    }
#endif

    classic.clGetPlatformInfo = fake_get_platform_info;
    classic.clRetainContext = fake_retain_context;
    classic.clCreateBuffer = fake_create_buffer;
    classic.clEnqueueNDRangeKernel = fake_enqueue_nd_range_kernel;
    classic.clSetContextDestructorCallback = fake_set_context_destructor_callback;
    classic.clGetExtensionFunctionAddressForPlatform =
        fake_get_extension_function_address_for_platform;
    classic.clCreateContextFromType = fake_create_context_from_type;
    icd2_tag.clGetPlatformIDs = (clGetPlatformIDs_t *)CL_ICD2_TAG_KHR;
    icd2_tag.clRetainContext = tagged_retain_context;

    check_dispatch();
#if !defined(_WIN32)
    check_initialized();
#endif
    printf("opencl-icd-loader (C): %s\n", ok ? "ok" : "FAILED");
    return ok ? 0 : 1;
}
