// A C++ producer exports framework-owned tensors through the exchange-API
// table in the shape dlpack.h's DLPackExchangeAPI documentation gives; a
// consumer holds them in RAII handles and reads them through their strides.
#include <dlpack/dlpack.h>

#include <complex>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

static_assert(std::is_same<std::underlying_type<DLDeviceType>::type, std::int32_t>::value,
              "DLDeviceType has a fixed int32_t underlying type in C++");
static_assert(std::is_standard_layout<DLTensor>::value && std::is_trivial<DLTensor>::value,
              "DLTensor is a C struct");
static_assert(std::is_standard_layout<DLManagedTensorVersioned>::value &&
                  std::is_trivial<DLManagedTensorVersioned>::value,
              "DLManagedTensorVersioned is a C struct");
static_assert(DLPACK_MAJOR_VERSION == 1 && DLPACK_MINOR_VERSION == 3, "DLPack 1.3");
static_assert(kDLCPU == 1 && kDLCUDA == 2 && kDLMetal == 8 && kDLOneAPI == 14 && kDLTrn == 18 &&
                  kDLInt == 0 && kDLComplex == 5 && kDLBool == 6 && kDLFloat4_e2m1fn == 17,
              "ABI enumerator values");
static_assert(sizeof(DLDataType) == 4 && sizeof(DLDevice) == 8, "fixed-size ABI members");
static_assert(sizeof(std::complex<float>) == 8, "complex64 is two packed floats");
#if UINTPTR_MAX == UINT64_MAX
static_assert(sizeof(DLTensor) == 48 && offsetof(DLTensor, byte_offset) == 40, "64-bit DLTensor");
static_assert(offsetof(DLManagedTensorVersioned, flags) == 24 &&
                  offsetof(DLManagedTensorVersioned, dl_tensor) == 32,
              "64-bit DLManagedTensorVersioned");
#endif

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("check failed: %s\n", what);
        ++failures;
    }
}

// The producer framework's tensor: owns its storage, shape and strides.
struct FrameworkTensor {
    std::vector<std::complex<float>> storage;
    std::vector<std::int64_t> shape;
    std::vector<std::int64_t> strides;
};

int live_exports = 0;
int deleter_calls = 0;

struct Export {
    std::shared_ptr<FrameworkTensor> owner;
    DLManagedTensorVersioned managed;
};

void delete_export(DLManagedTensorVersioned* self) {
    delete static_cast<Export*>(self->manager_ctx);
    --live_exports;
    ++deleter_calls;
}

DLManagedTensorVersioned* export_tensor(const std::shared_ptr<FrameworkTensor>& tensor) {
    Export* e = new Export();
    e->owner = tensor;
    DLManagedTensorVersioned& m = e->managed;
    m.version.major = DLPACK_MAJOR_VERSION;
    m.version.minor = DLPACK_MINOR_VERSION;
    m.manager_ctx = e;
    m.deleter = delete_export;
    m.flags = 0;
    m.dl_tensor.data = tensor->storage.data();
    m.dl_tensor.device.device_type = kDLCPU;
    m.dl_tensor.device.device_id = 0;
    m.dl_tensor.ndim = static_cast<std::int32_t>(tensor->shape.size());
    m.dl_tensor.dtype.code = kDLComplex;
    m.dl_tensor.dtype.bits = 64;
    m.dl_tensor.dtype.lanes = 1;
    m.dl_tensor.shape = tensor->shape.data();
    m.dl_tensor.strides = tensor->strides.data();
    m.dl_tensor.byte_offset = 0;
    ++live_exports;
    return &m;
}

int from_object(void* object, DLManagedTensorVersioned** out) {
    *out = export_tensor(*static_cast<std::shared_ptr<FrameworkTensor>*>(object));
    return 0;
}

// Steals the tensor: the new object shares the framework
// tensor, and the export is released.
int to_object(DLManagedTensorVersioned* tensor, void** out) {
    *out = new std::shared_ptr<FrameworkTensor>(static_cast<Export*>(tensor->manager_ctx)->owner);
    tensor->deleter(tensor);
    return 0;
}

int stream(DLDeviceType device_type, std::int32_t, void** out) {
    *out = nullptr;
    return device_type == kDLCPU ? 0 : -1;
}

int allocate(DLTensor*, DLManagedTensorVersioned** out, void* error_ctx,
             void (*set_error)(void*, const char*, const char*)) {
    *out = nullptr;
    set_error(error_ctx, "NotImplementedError", "this producer only exports");
    return -1;
}

// The pattern dlpack.h documents for the producer's static table.
struct MyDLPackExchangeAPI : public DLPackExchangeAPI {
    MyDLPackExchangeAPI() {
        header.version.major = DLPACK_MAJOR_VERSION;
        header.version.minor = DLPACK_MINOR_VERSION;
        header.prev_api = nullptr;
        managed_tensor_allocator = allocate;
        managed_tensor_from_py_object_no_sync = from_object;
        managed_tensor_to_py_object_no_sync = to_object;
        dltensor_from_py_object_no_sync = nullptr;
        current_work_stream = stream;
    }

    static const DLPackExchangeAPI* Global() {
        static MyDLPackExchangeAPI inst;
        return &inst;
    }
};

struct ReleaseTensor {
    void operator()(DLManagedTensorVersioned* t) const {
        if (t->deleter != nullptr) t->deleter(t);
    }
};
using TensorHandle = std::unique_ptr<DLManagedTensorVersioned, ReleaseTensor>;

void record_error(void* ctx, const char* kind, const char*) {
    *static_cast<std::string*>(ctx) = kind;
}

}  // namespace

int main() {
    const DLPackExchangeAPI* api = MyDLPackExchangeAPI::Global();
    check(api->header.version.major == DLPACK_MAJOR_VERSION, "table major version");
    check(api->dltensor_from_py_object_no_sync == nullptr, "optional entry left null");

    // A 2x2 complex matrix stored column-major: element (i, j) at i + 2 * j.
    auto tensor = std::make_shared<FrameworkTensor>();
    tensor->storage = {{1, 1}, {2, -1}, {3, 0.5f}, {4, 0}};
    tensor->shape = {2, 2};
    tensor->strides = {1, 2};

    {
        DLManagedTensorVersioned* raw = nullptr;
        check(api->managed_tensor_from_py_object_no_sync(&tensor, &raw) == 0, "export");
        TensorHandle handle(raw);
        check(live_exports == 1 && tensor.use_count() == 2, "export holds the tensor");
        check(handle->version.major == DLPACK_MAJOR_VERSION, "tensor major version");
        const DLTensor& t = handle->dl_tensor;
        check(t.dtype.code == kDLComplex && t.dtype.bits == 64 && t.dtype.lanes == 1,
              "complex64 dtype");
        const auto* data = reinterpret_cast<const std::complex<float>*>(
            static_cast<const char*>(t.data) + t.byte_offset);
        std::complex<float> trace = data[0 * t.strides[0] + 0 * t.strides[1]] +
                                    data[1 * t.strides[0] + 1 * t.strides[1]];
        check(trace == std::complex<float>(5, 1), "trace through strides");
        check(data[0 * t.strides[0] + 1 * t.strides[1]] == std::complex<float>(3, 0.5f),
              "element (0, 1)");
    }
    check(live_exports == 0 && deleter_calls == 1 && tensor.use_count() == 1,
          "the handle released the export once");

    {
        DLManagedTensorVersioned* raw = nullptr;
        check(api->managed_tensor_from_py_object_no_sync(&tensor, &raw) == 0, "second export");
        TensorHandle handle(raw);
        void* back = nullptr;
        check(api->managed_tensor_to_py_object_no_sync(handle.release(), &back) == 0, "import");
        std::unique_ptr<std::shared_ptr<FrameworkTensor>> object(
            static_cast<std::shared_ptr<FrameworkTensor>*>(back));
        check(object && object->get() == tensor.get() && tensor.use_count() == 2,
              "the imported object shares the framework tensor");
        check(live_exports == 0 && deleter_calls == 2, "the import consumed the export once");
    }
    check(tensor.use_count() == 1, "the imported object released the tensor");

    void* cuda_stream = &failures;
    check(api->current_work_stream(kDLCPU, 0, &cuda_stream) == 0 && cuda_stream == nullptr,
          "CPU has no stream");
    check(api->current_work_stream(kDLCUDA, 0, &cuda_stream) == -1, "CUDA unsupported here");

    std::string error;
    DLManagedTensorVersioned* none = nullptr;
    DLTensor prototype;
    std::memset(&prototype, 0, sizeof prototype);
    check(api->managed_tensor_allocator(&prototype, &none, &error, record_error) == -1 &&
              none == nullptr && error == "NotImplementedError",
          "allocator reports through SetError");

    if (failures != 0) {
        std::printf("dlpack: %d failures\n", failures);
        return 1;
    }
    std::printf("dlpack: ok\n");
    return 0;
}
