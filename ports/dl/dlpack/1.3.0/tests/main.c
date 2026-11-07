/* A producer exports a strided view through DLManagedTensorVersioned and the
 * DLPackExchangeAPI function table; a consumer checks the version, reads the
 * view through its strides and releases it through the deleter. */
#include <dlpack/dlpack.h>

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond)                                                         \
    do {                                                                    \
        if (!(cond)) {                                                      \
            printf("%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                     \
        }                                                                   \
    } while (0)

/* The size of a tensor's contents, as dlpack.h:237-244 defines it. */
static size_t data_size(const DLTensor *t) {
    size_t size = 1;
    int32_t i;
    for (i = 0; i < t->ndim; ++i) size *= (size_t)t->shape[i];
    size *= ((size_t)t->dtype.bits * t->dtype.lanes + 7) / 8;
    return size;
}

/* The producer's array type, standing in for a framework's tensor object. */
typedef struct {
    float values[6];
    int64_t shape[2];
    int64_t strides[2];
    int64_t row_shape[2];
    int exported;
} Array;

static int deleter_calls = 0;

typedef struct {
    int64_t shape[2];
    int64_t strides[2];
    Array *owner;
    void *allocation;
} ExportContext;

static void delete_versioned(DLManagedTensorVersioned *self) {
    ExportContext *ctx = (ExportContext *)self->manager_ctx;
    ++deleter_calls;
    if (ctx->owner != NULL) --ctx->owner->exported;
    free(ctx->allocation);
    free(ctx);
    free(self);
}

/* Exports the transpose of a row-major 2x3 array: shape {3, 2}, strides {1, 3}. */
static int from_py_object(void *py_object, DLManagedTensorVersioned **out) {
    Array *array = (Array *)py_object;
    DLManagedTensorVersioned *t = (DLManagedTensorVersioned *)calloc(1, sizeof *t);
    ExportContext *ctx = (ExportContext *)calloc(1, sizeof *ctx);
    if (t == NULL || ctx == NULL) {
        free(t);
        free(ctx);
        return -1;
    }
    ctx->shape[0] = array->shape[1];
    ctx->shape[1] = array->shape[0];
    ctx->strides[0] = array->strides[1];
    ctx->strides[1] = array->strides[0];
    ctx->owner = array;
    ++array->exported;
    t->version.major = DLPACK_MAJOR_VERSION;
    t->version.minor = DLPACK_MINOR_VERSION;
    t->manager_ctx = ctx;
    t->deleter = delete_versioned;
    t->flags = DLPACK_FLAG_BITMASK_READ_ONLY;
    t->dl_tensor.data = array->values;
    t->dl_tensor.device.device_type = kDLCPU;
    t->dl_tensor.device.device_id = 0;
    t->dl_tensor.ndim = 2;
    t->dl_tensor.dtype.code = kDLFloat;
    t->dl_tensor.dtype.bits = 32;
    t->dl_tensor.dtype.lanes = 1;
    t->dl_tensor.shape = ctx->shape;
    t->dl_tensor.strides = ctx->strides;
    t->dl_tensor.byte_offset = 0;
    *out = t;
    return 0;
}

static int dltensor_from_py_object(void *py_object, DLTensor *out) {
    Array *array = (Array *)py_object;
    out->data = array->values;
    out->device.device_type = kDLCPU;
    out->device.device_id = 0;
    out->ndim = 2;
    out->dtype.code = kDLFloat;
    out->dtype.bits = 32;
    out->dtype.lanes = 1;
    /* The 1x3 view of the second row: byte_offset skips the first. */
    out->shape = array->row_shape;
    out->strides = array->strides;
    out->byte_offset = 3 * sizeof(float);
    return 0;
}

static const char *last_error_kind = NULL;

static void set_error(void *error_ctx, const char *kind, const char *message) {
    (void)message;
    *(int *)error_ctx += 1;
    last_error_kind = kind;
}

/* Allocates a contiguous copy-owned tensor shaped like the prototype; CPU only. */
static int allocator(DLTensor *prototype, DLManagedTensorVersioned **out, void *error_ctx,
                     void (*SetError)(void *error_ctx, const char *kind, const char *message)) {
    DLManagedTensorVersioned *t;
    ExportContext *ctx;
    size_t count = 1;
    int32_t i;
    if (prototype->device.device_type != kDLCPU || prototype->ndim > 2) {
        SetError(error_ctx, "BufferError", "unsupported prototype");
        *out = NULL;
        return -1;
    }
    t = (DLManagedTensorVersioned *)calloc(1, sizeof *t);
    ctx = (ExportContext *)calloc(1, sizeof *ctx);
    for (i = 0; i < prototype->ndim; ++i) count *= (size_t)prototype->shape[i];
    if (t != NULL && ctx != NULL)
        ctx->allocation = calloc(count, ((size_t)prototype->dtype.bits * prototype->dtype.lanes + 7) / 8);
    if (t == NULL || ctx == NULL || ctx->allocation == NULL) {
        free(t);
        if (ctx != NULL) free(ctx->allocation);
        free(ctx);
        SetError(error_ctx, "MemoryError", "out of memory");
        *out = NULL;
        return -1;
    }
    for (i = prototype->ndim - 1; i >= 0; --i) {
        ctx->shape[i] = prototype->shape[i];
        ctx->strides[i] = i == prototype->ndim - 1 ? 1 : ctx->strides[i + 1] * ctx->shape[i + 1];
    }
    t->version.major = DLPACK_MAJOR_VERSION;
    t->version.minor = DLPACK_MINOR_VERSION;
    t->manager_ctx = ctx;
    t->deleter = delete_versioned;
    t->flags = DLPACK_FLAG_BITMASK_IS_COPIED;
    t->dl_tensor = *prototype;
    t->dl_tensor.data = ctx->allocation;
    t->dl_tensor.shape = ctx->shape;
    t->dl_tensor.strides = ctx->strides;
    t->dl_tensor.byte_offset = 0;
    *out = t;
    return 0;
}

static int16_t imported_last = 0;

/* Takes ownership of an int16 tensor: copies out its last element as the
 * "object" and releases the tensor. */
static int to_py_object(DLManagedTensorVersioned *tensor, void **out_py_object) {
    const DLTensor *t = &tensor->dl_tensor;
    const int16_t *data;
    if (t->dtype.code != kDLInt || t->dtype.bits != 16 || t->dtype.lanes != 1) return -1;
    data = (const int16_t *)((const char *)t->data + t->byte_offset);
    imported_last = data[data_size(t) / sizeof(int16_t) - 1];
    *out_py_object = &imported_last;
    tensor->deleter(tensor);
    return 0;
}

static int current_work_stream(DLDeviceType device_type, int32_t device_id, void **out_current_stream) {
    (void)device_id;
    if (device_type != kDLCPU) return -1;
    *out_current_stream = NULL;
    return 0;
}

static float element(const DLTensor *t, int64_t i, int64_t j) {
    const char *base = (const char *)t->data + t->byte_offset;
    return ((const float *)base)[i * t->strides[0] + j * t->strides[1]];
}

static void check_abi(void) {
    static const int32_t devices[] = {kDLCPU,     kDLCUDA,     kDLCUDAHost,    kDLOpenCL, kDLVulkan,
                                      kDLMetal,   kDLVPI,      kDLROCM,        kDLROCMHost, kDLExtDev,
                                      kDLCUDAManaged, kDLOneAPI, kDLWebGPU,    kDLHexagon, kDLMAIA,
                                      kDLTrn};
    static const int32_t device_codes[] = {1, 2, 3, 4, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18};
    static const uint8_t types[] = {kDLInt,          kDLUInt,           kDLFloat,
                                    kDLOpaqueHandle, kDLBfloat,         kDLComplex,
                                    kDLBool,         kDLFloat8_e3m4,    kDLFloat8_e4m3,
                                    kDLFloat8_e4m3b11fnuz, kDLFloat8_e4m3fn, kDLFloat8_e4m3fnuz,
                                    kDLFloat8_e5m2,  kDLFloat8_e5m2fnuz, kDLFloat8_e8m0fnu,
                                    kDLFloat6_e2m3fn, kDLFloat6_e3m2fn, kDLFloat4_e2m1fn};
    size_t i;

    CHECK(DLPACK_MAJOR_VERSION == 1 && DLPACK_MINOR_VERSION == 3);
    for (i = 0; i < sizeof devices / sizeof devices[0]; ++i) CHECK(devices[i] == device_codes[i]);
    for (i = 0; i < sizeof types / sizeof types[0]; ++i) CHECK(types[i] == i);
    CHECK(DLPACK_FLAG_BITMASK_READ_ONLY == 1 && DLPACK_FLAG_BITMASK_IS_COPIED == 2 &&
          DLPACK_FLAG_BITMASK_IS_SUBBYTE_TYPE_PADDED == 4);

    CHECK(sizeof(DLPackVersion) == 8 && sizeof(DLDevice) == 8 && sizeof(DLDataType) == 4);
    CHECK(offsetof(DLDataType, bits) == 1 && offsetof(DLDataType, lanes) == 2);
#if UINTPTR_MAX == UINT64_MAX
    /* The layouts a 64-bit consumer of another framework's tensors relies on. */
    CHECK(offsetof(DLTensor, device) == 8 && offsetof(DLTensor, ndim) == 16 &&
          offsetof(DLTensor, dtype) == 20 && offsetof(DLTensor, shape) == 24 &&
          offsetof(DLTensor, strides) == 32 && offsetof(DLTensor, byte_offset) == 40 &&
          sizeof(DLTensor) == 48);
    CHECK(offsetof(DLManagedTensor, manager_ctx) == 48 && offsetof(DLManagedTensor, deleter) == 56 &&
          sizeof(DLManagedTensor) == 64);
    CHECK(offsetof(DLManagedTensorVersioned, manager_ctx) == 8 &&
          offsetof(DLManagedTensorVersioned, deleter) == 16 &&
          offsetof(DLManagedTensorVersioned, flags) == 24 &&
          offsetof(DLManagedTensorVersioned, dl_tensor) == 32 && sizeof(DLManagedTensorVersioned) == 80);
    CHECK(offsetof(DLPackExchangeAPIHeader, prev_api) == 8 && sizeof(DLPackExchangeAPIHeader) == 16);
    CHECK(offsetof(DLPackExchangeAPI, managed_tensor_allocator) == 16 &&
          offsetof(DLPackExchangeAPI, current_work_stream) == 48 && sizeof(DLPackExchangeAPI) == 56);
#endif
}

static int legacy_deleted = 0;

static void delete_legacy(DLManagedTensor *self) {
    ++legacy_deleted;
    free(self);
}

int main(void) {
    static const DLPackExchangeAPI api = {
        {{DLPACK_MAJOR_VERSION, DLPACK_MINOR_VERSION}, NULL},
        allocator,
        from_py_object,
        to_py_object,
        dltensor_from_py_object,
        current_work_stream,
    };
    const DLPackExchangeAPI *found = &api;
    Array array = {{0, 1, 2, 3, 4, 5}, {2, 3}, {3, 1}, {1, 3}, 0};
    int64_t legacy_shape[1] = {3};
    int64_t legacy_strides[1] = {1};
    DLManagedTensorVersioned *view = NULL;
    DLManagedTensorVersioned *fresh = NULL;
    DLManagedTensor *legacy;
    DLTensor borrowed;
    DLTensor prototype;
    int64_t prototype_shape[2] = {4, 5};
    int errors = 0;
    void *stream = &errors;
    void *py_object = NULL;
    int64_t i, j;

    check_abi();

    /* The consumer checks the table's major version before using it. */
    CHECK(found->header.version.major == DLPACK_MAJOR_VERSION && found->header.prev_api == NULL);
    CHECK(found->current_work_stream(kDLCPU, 0, &stream) == 0 && stream == NULL);
    CHECK(found->current_work_stream(kDLCUDA, 0, &stream) == -1);

    /* An owning, read-only transposed view: element (i, j) is array[j][i]. */
    CHECK(found->managed_tensor_from_py_object_no_sync(&array, &view) == 0 && view != NULL);
    if (view != NULL) {
        CHECK(view->version.major == DLPACK_MAJOR_VERSION);
        CHECK((view->flags & DLPACK_FLAG_BITMASK_READ_ONLY) != 0);
        CHECK((view->flags & DLPACK_FLAG_BITMASK_IS_COPIED) == 0);
        CHECK(view->dl_tensor.shape[0] == 3 && view->dl_tensor.shape[1] == 2);
        CHECK(data_size(&view->dl_tensor) == 24);
        for (i = 0; i < 3; ++i)
            for (j = 0; j < 2; ++j) CHECK(element(&view->dl_tensor, i, j) == array.values[j * 3 + i]);
        CHECK(array.exported == 1);
        view->deleter(view);
        CHECK(deleter_calls == 1 && array.exported == 0);
    }

    /* A non-owning view of the second row, through byte_offset. */
    CHECK(found->dltensor_from_py_object_no_sync(&array, &borrowed) == 0);
    CHECK(borrowed.shape[0] == 1 && borrowed.shape[1] == 3 && data_size(&borrowed) == 12);
    for (j = 0; j < 3; ++j) CHECK(element(&borrowed, 0, j) == array.values[3 + j]);

    /* A producer-allocated tensor from a prototype, and a rejected one. */
    memset(&prototype, 0, sizeof prototype);
    prototype.device.device_type = kDLCPU;
    prototype.ndim = 2;
    prototype.dtype.code = kDLInt;
    prototype.dtype.bits = 16;
    prototype.dtype.lanes = 1;
    prototype.shape = prototype_shape;
    CHECK(found->managed_tensor_allocator(&prototype, &fresh, &errors, set_error) == 0 && fresh != NULL);
    if (fresh != NULL) {
        CHECK((fresh->flags & DLPACK_FLAG_BITMASK_IS_COPIED) != 0);
        CHECK(fresh->dl_tensor.strides[0] == 5 && fresh->dl_tensor.strides[1] == 1);
        CHECK(data_size(&fresh->dl_tensor) == 40);
        ((int16_t *)fresh->dl_tensor.data)[19] = -7;
        CHECK(found->managed_tensor_to_py_object_no_sync(fresh, &py_object) == 0);
        CHECK(py_object == &imported_last && imported_last == -7);
        CHECK(deleter_calls == 2);
    }
    prototype.device.device_type = kDLCUDA;
    CHECK(found->managed_tensor_allocator(&prototype, &fresh, &errors, set_error) == -1 && fresh == NULL);
    CHECK(errors == 1 && last_error_kind != NULL && strcmp(last_error_kind, "BufferError") == 0);

    /* The legacy, unversioned DLManagedTensor: three float2 vectors. */
    legacy = (DLManagedTensor *)calloc(1, sizeof *legacy);
    if (legacy != NULL) {
        legacy->dl_tensor.data = array.values;
        legacy->dl_tensor.device.device_type = kDLCPU;
        legacy->dl_tensor.ndim = 1;
        legacy->dl_tensor.dtype.code = kDLFloat;
        legacy->dl_tensor.dtype.bits = 32;
        legacy->dl_tensor.dtype.lanes = 2;
        legacy->dl_tensor.shape = legacy_shape;
        legacy->dl_tensor.strides = legacy_strides;
        legacy->deleter = delete_legacy;
        CHECK(data_size(&legacy->dl_tensor) == sizeof array.values);
        for (j = 0; j < legacy_shape[0] * legacy->dl_tensor.dtype.lanes; ++j)
            CHECK(((const float *)legacy->dl_tensor.data)[j] == (float)j);
        legacy->deleter(legacy);
    }
    CHECK(legacy_deleted == 1);

    if (failures != 0) {
        printf("dlpack: %d failures\n", failures);
        return 1;
    }
    printf("dlpack: ok\n");
    return 0;
}
