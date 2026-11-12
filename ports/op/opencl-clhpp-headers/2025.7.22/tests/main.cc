// Runs the C++ bindings against fake OpenCL entry points, since CI machines
// have no OpenCL driver for a loader to find: platform and device
// enumeration, the default platform, string, scalar, array and object info
// queries, device reference counting, cl::Error from a failing call, and how
// Kernel::setArg passes a scalar, a buffer and local memory to
// clSetKernelArg.
#define CL_TARGET_OPENCL_VERSION 300
#define CL_HPP_TARGET_OPENCL_VERSION 300
#define CL_HPP_ENABLE_EXCEPTIONS
// First, so that on Apple opencl.hpp's <OpenCL/opencl.h> is a no-op rather
// than the SDK's OpenCL 1.2 headers (see cabin.toml).
#include <CL/opencl.h>
#include <CL/opencl.hpp>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct _cl_platform_id {
  const char* name;
};
struct _cl_device_id {
  cl_device_type type;
  int refs;
};
struct _cl_kernel {
  int refs;
};
struct _cl_mem {
  int refs;
};

namespace {

bool ok = true;

void check(bool pass, const char* what) {
  if (!pass) {
    fprintf(stderr, "FAIL: %s\n", what);
    ok = false;
  }
}

_cl_platform_id fake_platforms[] = {{"Fake platform"}, {"Empty platform"}};
_cl_device_id fake_devices[] = {
    {CL_DEVICE_TYPE_GPU, 0}, {CL_DEVICE_TYPE_CPU, 0}, {CL_DEVICE_TYPE_GPU, 0}};
// One reference each, as clCreateKernel and clCreateBuffer return them.
_cl_kernel fake_kernel = {1};
_cl_mem fake_mem = {1};

struct {
  cl_uint index;
  size_t size;
  const void* value;
} last_arg;

cl_int info(const void* src, size_t n, size_t size, void* value, size_t* size_ret) {
  if (value) {
    if (size < n) return CL_INVALID_VALUE;
    memcpy(value, src, n);
  }
  if (size_ret) *size_ret = n;
  return CL_SUCCESS;
}

}  // namespace

extern "C" {

CL_API_ENTRY cl_int CL_API_CALL clGetPlatformIDs(cl_uint num_entries, cl_platform_id* platforms,
                                                 cl_uint* num_platforms) {
  for (cl_uint i = 0; i < num_entries && i < 2; ++i) platforms[i] = &fake_platforms[i];
  if (num_platforms) *num_platforms = 2;
  return CL_SUCCESS;
}

CL_API_ENTRY cl_int CL_API_CALL clGetPlatformInfo(cl_platform_id platform, cl_platform_info param,
                                                  size_t size, void* value, size_t* size_ret) {
  if (param != CL_PLATFORM_NAME) return CL_INVALID_VALUE;
  return info(platform->name, strlen(platform->name) + 1, size, value, size_ret);
}

CL_API_ENTRY cl_int CL_API_CALL clGetDeviceIDs(cl_platform_id platform, cl_device_type type,
                                               cl_uint num_entries, cl_device_id* devices,
                                               cl_uint* num_devices) {
  cl_uint n = 0;
  if (platform == &fake_platforms[0]) {
    for (_cl_device_id& d : fake_devices) {
      if (!(d.type & type)) continue;
      if (n < num_entries) devices[n] = &d;
      ++n;
    }
  }
  if (num_devices) *num_devices = n;
  return n ? CL_SUCCESS : CL_DEVICE_NOT_FOUND;
}

CL_API_ENTRY cl_int CL_API_CALL clGetDeviceInfo(cl_device_id device, cl_device_info param,
                                                size_t size, void* value, size_t* size_ret) {
  static const size_t work_item_sizes[] = {1024, 64, 16};
  const cl_platform_id platform = &fake_platforms[0];
  switch (param) {
    case CL_DEVICE_TYPE:
      return info(&device->type, sizeof device->type, size, value, size_ret);
    case CL_DEVICE_MAX_WORK_ITEM_SIZES:
      return info(work_item_sizes, sizeof work_item_sizes, size, value, size_ret);
    case CL_DEVICE_PLATFORM:
      return info(&platform, sizeof platform, size, value, size_ret);
    default:
      return CL_INVALID_VALUE;
  }
}

CL_API_ENTRY cl_int CL_API_CALL clRetainDevice(cl_device_id device) {
  ++device->refs;
  return CL_SUCCESS;
}

CL_API_ENTRY cl_int CL_API_CALL clReleaseDevice(cl_device_id device) {
  --device->refs;
  return CL_SUCCESS;
}

CL_API_ENTRY cl_int CL_API_CALL clSetKernelArg(cl_kernel, cl_uint index, size_t size,
                                               const void* value) {
  last_arg.index = index;
  last_arg.size = size;
  last_arg.value = value;
  return index < 3 ? CL_SUCCESS : CL_INVALID_ARG_INDEX;
}

CL_API_ENTRY cl_int CL_API_CALL clRetainKernel(cl_kernel kernel) {
  ++kernel->refs;
  return CL_SUCCESS;
}

CL_API_ENTRY cl_int CL_API_CALL clReleaseKernel(cl_kernel kernel) {
  --kernel->refs;
  return CL_SUCCESS;
}

CL_API_ENTRY cl_int CL_API_CALL clRetainMemObject(cl_mem mem) {
  ++mem->refs;
  return CL_SUCCESS;
}

CL_API_ENTRY cl_int CL_API_CALL clReleaseMemObject(cl_mem mem) {
  --mem->refs;
  return CL_SUCCESS;
}

// Referenced by the default context and queue opencl.hpp defines; nothing
// here creates either, so neither may run.  They would run as those statics
// are destroyed after main returns, so they abort rather than clear `ok`.
CL_API_ENTRY cl_int CL_API_CALL clReleaseContext(cl_context) {
  fprintf(stderr, "FAIL: unexpected clReleaseContext\n");
  abort();
}

CL_API_ENTRY cl_int CL_API_CALL clReleaseCommandQueue(cl_command_queue) {
  fprintf(stderr, "FAIL: unexpected clReleaseCommandQueue\n");
  abort();
}

}  // extern "C"

int main() {
  {
    cl::vector<cl::Platform> platforms;
    check(cl::Platform::get(&platforms) == CL_SUCCESS && platforms.size() == 2 &&
              platforms[0]() == &fake_platforms[0] && platforms[1]() == &fake_platforms[1],
          "Platform::get");
    check(platforms[0].getInfo<CL_PLATFORM_NAME>() == "Fake platform", "platform name");
    check(cl::Platform::getDefault()() == &fake_platforms[0], "default platform");

    cl::vector<cl::Device> gpus;
    check(platforms[0].getDevices(CL_DEVICE_TYPE_GPU, &gpus) == CL_SUCCESS && gpus.size() == 2 &&
              gpus[0]() == &fake_devices[0] && gpus[1]() == &fake_devices[2],
          "GPU devices");
    check(fake_devices[0].refs == 1 && fake_devices[1].refs == 0 && fake_devices[2].refs == 1,
          "devices retained once");
    {
      cl::Device copy = gpus[1];
      check(fake_devices[2].refs == 2, "copy retains");
    }
    check(fake_devices[2].refs == 1, "copy releases");

    check(gpus[0].getInfo<CL_DEVICE_TYPE>() == CL_DEVICE_TYPE_GPU, "device type");
    const cl::vector<cl::size_type> sizes = gpus[1].getInfo<CL_DEVICE_MAX_WORK_ITEM_SIZES>();
    check(sizes.size() == 3 && sizes[0] == 1024 && sizes[1] == 64 && sizes[2] == 16,
          "max work-item sizes");
    check(gpus[0].getInfo<CL_DEVICE_PLATFORM>()() == &fake_platforms[0], "device platform");

    cl::vector<cl::Device> none;
    check(platforms[1].getDevices(CL_DEVICE_TYPE_ALL, &none) == CL_SUCCESS && none.empty(),
          "CL_DEVICE_NOT_FOUND gives no devices");

    try {
      platforms[0].getInfo<CL_PLATFORM_VENDOR>();
      check(false, "unsupported query throws");
    } catch (const cl::Error& e) {
      check(e.err() == CL_INVALID_VALUE && strcmp(e.what(), "clGetPlatformInfo") == 0,
            "cl::Error from clGetPlatformInfo");
    }

    cl::Kernel kernel(&fake_kernel);
    cl::Buffer buffer(&fake_mem);
    const cl_float scale = 0.5f;
    kernel.setArg(0, scale);
    check(last_arg.index == 0 && last_arg.size == sizeof scale && last_arg.value == &scale,
          "scalar argument");
    kernel.setArg(1, buffer);
    check(last_arg.index == 1 && last_arg.size == sizeof(cl_mem) &&
              *static_cast<const cl_mem*>(last_arg.value) == &fake_mem,
          "buffer argument");
    kernel.setArg(2, cl::Local(256));
    check(last_arg.index == 2 && last_arg.size == 256 && last_arg.value == nullptr,
          "local memory argument");
    try {
      kernel.setArg(3, scale);
      check(false, "bad argument index throws");
    } catch (const cl::Error& e) {
      check(e.err() == CL_INVALID_ARG_INDEX && strcmp(e.what(), "clSetKernelArg") == 0,
            "cl::Error from clSetKernelArg");
    }
  }
  check(fake_devices[0].refs == 0 && fake_devices[2].refs == 0 && fake_kernel.refs == 0 &&
            fake_mem.refs == 0,
        "wrappers release what they own");

  printf("opencl-clhpp-headers: %s\n", ok ? "ok" : "FAILED");
  return ok ? 0 : 1;
}
