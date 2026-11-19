/* Temporary CI probe: lists every OpenCL platform and device this machine
 * offers and runs a small kernel on each device, checking its result. */
#define CL_TARGET_OPENCL_VERSION 120
#ifdef __APPLE__
#include <OpenCL/opencl.h>
#else
#include <CL/cl.h>
#endif
#include <stdio.h>

static const char *source =
    "__kernel void twice(__global int *a) { size_t i = get_global_id(0); a[i] = 2 * a[i] + 1; }";

static int run_kernel(cl_device_id device) {
    enum { N = 4096 };
    static int data[N];
    cl_int err;
    size_t global = N;
    int i, bad = 0;
    cl_context context = clCreateContext(NULL, 1, &device, NULL, NULL, &err);
    if (!context) return printf("    clCreateContext: %d\n", err), 1;
    cl_command_queue queue = clCreateCommandQueue(context, device, 0, &err);
    if (!queue) return printf("    clCreateCommandQueue: %d\n", err), 1;
    cl_program program = clCreateProgramWithSource(context, 1, &source, NULL, &err);
    if (!program) return printf("    clCreateProgramWithSource: %d\n", err), 1;
    if ((err = clBuildProgram(program, 1, &device, NULL, NULL, NULL)) != CL_SUCCESS) {
        char log[4096] = "";
        clGetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_LOG, sizeof log - 1, log, NULL);
        return printf("    clBuildProgram: %d\n%s\n", err, log), 1;
    }
    cl_kernel kernel = clCreateKernel(program, "twice", &err);
    if (!kernel) return printf("    clCreateKernel: %d\n", err), 1;
    for (i = 0; i < N; i++) data[i] = i;
    cl_mem buffer = clCreateBuffer(context, CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR, sizeof data, data, &err);
    if (!buffer) return printf("    clCreateBuffer: %d\n", err), 1;
    clSetKernelArg(kernel, 0, sizeof buffer, &buffer);
    if ((err = clEnqueueNDRangeKernel(queue, kernel, 1, NULL, &global, NULL, 0, NULL, NULL)) != CL_SUCCESS)
        return printf("    clEnqueueNDRangeKernel: %d\n", err), 1;
    if ((err = clEnqueueReadBuffer(queue, buffer, CL_TRUE, 0, sizeof data, data, 0, NULL, NULL)) != CL_SUCCESS)
        return printf("    clEnqueueReadBuffer: %d\n", err), 1;
    for (i = 0; i < N; i++) bad += data[i] != 2 * i + 1;
    printf("    kernel: %s (%d of %d wrong)\n", bad ? "WRONG" : "KERNEL OK", bad, N);
    return bad != 0;
}

int main(void) {
    cl_platform_id platforms[16];
    cl_uint np = 0, p;
    cl_int err = clGetPlatformIDs(16, platforms, &np);
    printf("clGetPlatformIDs: %d, %u platform(s)\n", err, np);
    for (p = 0; p < np; p++) {
        char name[256] = "", version[256] = "";
        cl_device_id devices[16];
        cl_uint nd = 0, d;
        clGetPlatformInfo(platforms[p], CL_PLATFORM_NAME, sizeof name, name, NULL);
        clGetPlatformInfo(platforms[p], CL_PLATFORM_VERSION, sizeof version, version, NULL);
        printf("platform %u: %s | %s\n", p, name, version);
        err = clGetDeviceIDs(platforms[p], CL_DEVICE_TYPE_ALL, 16, devices, &nd);
        printf("  clGetDeviceIDs: %d, %u device(s)\n", err, nd);
        for (d = 0; d < nd; d++) {
            char dname[256] = "", dver[256] = "";
            cl_device_type type = 0;
            clGetDeviceInfo(devices[d], CL_DEVICE_NAME, sizeof dname, dname, NULL);
            clGetDeviceInfo(devices[d], CL_DEVICE_VERSION, sizeof dver, dver, NULL);
            clGetDeviceInfo(devices[d], CL_DEVICE_TYPE, sizeof type, &type, NULL);
            printf("  device %u: %s | %s | type %#llx\n", d, dname, dver, (unsigned long long)type);
            run_kernel(devices[d]);
        }
    }
    return 0;
}
