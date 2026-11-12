// C++ consumer: drives the CPU backend through ggml-backend, ggml-alloc and
// gguf, holding everything in ggml-cpp.h's owning pointers, and checks the
// results against sums computed here.
#include <ggml.h>
#include <ggml-alloc.h>
#include <ggml-backend.h>
#include <ggml-cpp.h>
#include <ggml-cpu.h>
#include <gguf.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

int failures = 0;

#define CHECK(cond)                                                    \
  do {                                                                 \
    if (!(cond)) {                                                     \
      std::printf("main.cc:%d: check failed: %s\n", __LINE__, #cond);  \
      ++failures;                                                      \
    }                                                                  \
  } while (0)

bool near(double a, double b, double tol) { return std::fabs(a - b) <= tol; }

// A context that holds tensor metadata only; backend buffers hold the data.
ggml_context_ptr metadata_context() {
  ggml_init_params params = {16 * ggml_tensor_overhead() + ggml_graph_overhead(), nullptr, true};
  return ggml_context_ptr(ggml_init(params));
}

std::vector<float> wave(size_t n, double step, double scale) {
  std::vector<float> v(n);
  for (size_t i = 0; i < n; ++i) v[i] = static_cast<float>(std::sin(step * i + 0.3) * scale);
  return v;
}

// Allocates `graph` with ggml-alloc and runs it on `backend`.  The returned
// allocator owns the results' memory; it is null if a step failed.
ggml_gallocr_ptr compute(ggml_backend_t backend, ggml_cgraph *graph) {
  ggml_gallocr_ptr galloc(ggml_gallocr_new(ggml_backend_get_default_buffer_type(backend)));
  if (galloc && ggml_gallocr_alloc_graph(galloc.get(), graph) &&
      ggml_backend_graph_compute(backend, graph) == GGML_STATUS_SUCCESS) {
    return galloc;
  }
  return nullptr;
}

ggml_backend_dev_t test_registry() {
  CHECK(ggml_backend_reg_count() >= 1);
  ggml_backend_reg_t reg = ggml_backend_reg_by_name("CPU");
  CHECK(reg != nullptr);
  ggml_backend_dev_t dev = ggml_backend_dev_by_type(GGML_BACKEND_DEVICE_TYPE_CPU);
  CHECK(dev != nullptr && ggml_backend_dev_backend_reg(dev) == reg);
  return dev;
}

// softmax(0.5 * W x) with W in F32 and in F16.  The sizes are odd so the
// vector loops leave remainders, and the batch splits across threads.
void test_mul_mat(ggml_backend_t backend) {
  constexpr int K = 33, M = 17, N = 5;
  const std::vector<float> w = wave(K * M, 0.13, 1.0), x = wave(K * N, 0.07, 1.0);

  for (ggml_type type : {GGML_TYPE_F32, GGML_TYPE_F16}) {
    const bool half = type == GGML_TYPE_F16;
    ggml_context_ptr data = metadata_context();
    ggml_tensor *wt = ggml_new_tensor_2d(data.get(), type, K, M);
    ggml_tensor *xt = ggml_new_tensor_2d(data.get(), GGML_TYPE_F32, K, N);
    ggml_backend_buffer_ptr buffer(ggml_backend_alloc_ctx_tensors(data.get(), backend));
    CHECK(buffer != nullptr);
    if (!buffer) continue;
    // F16 weights take the input as F16 too: round both for the reference.
    std::vector<float> wr = w, xr = x;
    if (half) {
      std::vector<ggml_fp16_t> h(w.size());
      ggml_fp32_to_fp16_row(w.data(), h.data(), K * M);
      ggml_backend_tensor_set(wt, h.data(), 0, ggml_nbytes(wt));
      for (float &v : wr) v = ggml_fp16_to_fp32(ggml_fp32_to_fp16(v));
      for (float &v : xr) v = ggml_fp16_to_fp32(ggml_fp32_to_fp16(v));
    } else {
      ggml_backend_tensor_set(wt, w.data(), 0, ggml_nbytes(wt));
    }
    ggml_backend_tensor_set(xt, x.data(), 0, ggml_nbytes(xt));

    ggml_context_ptr ops = metadata_context();
    ggml_tensor *logits = ggml_scale(ops.get(), ggml_mul_mat(ops.get(), wt, xt), 0.5f);
    ggml_tensor *probs = ggml_soft_max(ops.get(), logits);
    ggml_set_output(logits);
    ggml_set_output(probs);
    ggml_cgraph *graph = ggml_new_graph(ops.get());
    ggml_build_forward_expand(graph, probs);
    ggml_gallocr_ptr results = compute(backend, graph);
    CHECK(results != nullptr);
    if (!results) continue;

    std::vector<float> got_logits(M * N), got_probs(M * N);
    ggml_backend_tensor_get(logits, got_logits.data(), 0, ggml_nbytes(logits));
    ggml_backend_tensor_get(probs, got_probs.data(), 0, ggml_nbytes(probs));
    for (int n = 0; n < N; ++n) {
      std::vector<double> ref(M);
      double max = -1e300, sum = 0;
      for (int m = 0; m < M; ++m) {
        double dot = 0;
        for (int k = 0; k < K; ++k) dot += double(wr[m * K + k]) * xr[n * K + k];
        ref[m] = 0.5 * dot;
        if (ref[m] > max) max = ref[m];
      }
      for (double r : ref) sum += std::exp(r - max);
      for (int m = 0; m < M; ++m) {
        CHECK(near(got_logits[n * M + m], ref[m], half ? 0.05 : 1e-4));
        CHECK(near(got_probs[n * M + m], std::exp(ref[m] - max) / sum, half ? 0.02 : 1e-5));
      }
    }
  }
}

// A Q4_0 weight in the CPU backend's repack buffer, which interleaves the
// blocks for the architecture's GEMM kernels where the CPU has them (Arm
// with dot-product instructions in this baseline build).  The answer must
// match the dequantized weights times the Q8_0-rounded input.
void test_repack(ggml_backend_dev_t dev, ggml_backend_t backend) {
  constexpr int K = 64, M = 8, N = 5;
  const std::vector<float> w = wave(K * M, 0.37, 1.0), x = wave(K * N, 0.11, 0.5);

  auto get_extra = reinterpret_cast<ggml_backend_dev_get_extra_bufts_t>(
      ggml_backend_reg_get_proc_address(ggml_backend_dev_backend_reg(dev), "ggml_backend_dev_get_extra_bufts"));
  CHECK(get_extra != nullptr);
  if (!get_extra) return;
  ggml_backend_buffer_type_t repack = nullptr;
  for (ggml_backend_buffer_type_t *b = get_extra(dev); *b; ++b) {
    if (std::string(ggml_backend_buft_name(*b)) == "CPU_REPACK") repack = *b;
  }
  CHECK(repack != nullptr);
  if (!repack) return;

  ggml_context_ptr weights = metadata_context(), inputs = metadata_context(), ops = metadata_context();
  ggml_tensor *wq = ggml_new_tensor_2d(weights.get(), GGML_TYPE_Q4_0, K, M);
  ggml_tensor *xt = ggml_new_tensor_2d(inputs.get(), GGML_TYPE_F32, K, N);
  ggml_backend_buffer_ptr wbuf(ggml_backend_alloc_ctx_tensors_from_buft(weights.get(), repack));
  ggml_backend_buffer_ptr xbuf(ggml_backend_alloc_ctx_tensors(inputs.get(), backend));
  CHECK(wbuf != nullptr && xbuf != nullptr);
  if (!wbuf || !xbuf) return;
  ggml_tensor *y = ggml_mul_mat(ops.get(), wq, xt);
  if (!ggml_backend_dev_supports_op(dev, y)) {
    std::printf("ggml (C++): no repacked kernel for this CPU\n");
    return;
  }

  std::vector<unsigned char> q(ggml_row_size(GGML_TYPE_Q4_0, K) * M);
  CHECK(ggml_quantize_chunk(GGML_TYPE_Q4_0, w.data(), q.data(), 0, M, K, nullptr) == q.size());
  ggml_backend_tensor_set(wq, q.data(), 0, q.size());
  ggml_backend_tensor_set(xt, x.data(), 0, ggml_nbytes(xt));
  ggml_cgraph *graph = ggml_new_graph(ops.get());
  ggml_build_forward_expand(graph, y);
  ggml_gallocr_ptr results = compute(backend, graph);
  CHECK(results != nullptr);
  if (!results) return;
  std::vector<float> got(M * N);
  ggml_backend_tensor_get(y, got.data(), 0, ggml_nbytes(y));

  std::vector<float> wd(K * M), xd(K);
  std::vector<unsigned char> xq(ggml_row_size(GGML_TYPE_Q8_0, K));
  ggml_get_type_traits(GGML_TYPE_Q4_0)->to_float(q.data(), wd.data(), K * M);
  for (int n = 0; n < N; ++n) {
    ggml_get_type_traits(GGML_TYPE_Q8_0)->from_float_ref(x.data() + n * K, xq.data(), K);
    ggml_get_type_traits(GGML_TYPE_Q8_0)->to_float(xq.data(), xd.data(), K);
    for (int m = 0; m < M; ++m) {
      double dot = 0;
      for (int k = 0; k < K; ++k) dot += double(wd[m * K + k]) * xd[k];
      CHECK(near(got[n * M + m], dot, 0.02));
    }
  }
  std::printf("ggml (C++): repacked Q4_0 kernel checked\n");
}

// A GGUF file built in memory: key-value pairs and one tensor, read back.
void test_gguf() {
  const float values[] = {1.5f, -2.0f, 3.25f, 0.0f};
  ggml_init_params params = {ggml_tensor_overhead() + sizeof values, nullptr, false};
  ggml_context_ptr ctx(ggml_init(params));
  ggml_tensor *t = ggml_new_tensor_1d(ctx.get(), GGML_TYPE_F32, 4);
  ggml_set_name(t, "weights");
  std::memcpy(t->data, values, sizeof values);

  gguf_context_ptr out(gguf_init_empty());
  gguf_set_val_str(out.get(), "general.name", "cabin");
  gguf_set_val_u32(out.get(), "test.answer", 42);
  gguf_add_tensor(out.get(), t);

  // The file is the metadata, then each tensor padded to the alignment.
  const size_t meta = gguf_get_meta_size(out.get());
  std::vector<unsigned char> file(meta + GGML_PAD(sizeof values, gguf_get_alignment(out.get())));
  gguf_get_meta_data(out.get(), file.data());
  CHECK(std::memcmp(file.data(), "GGUF", 4) == 0);
  std::memcpy(file.data() + meta + gguf_get_tensor_offset(out.get(), 0), values, sizeof values);

  ggml_context *loaded = nullptr;
  gguf_init_params in_params = {false, &loaded};
  gguf_context_ptr in(gguf_init_from_buffer(file.data(), file.size(), in_params));
  ggml_context_ptr loaded_ptr(loaded);
  CHECK(in != nullptr && loaded != nullptr);
  if (!in || !loaded) return;
  CHECK(gguf_get_version(in.get()) == 3);
  CHECK(gguf_get_n_kv(in.get()) == 2);
  const int64_t name = gguf_find_key(in.get(), "general.name");
  CHECK(name >= 0 && std::string(gguf_get_val_str(in.get(), name)) == "cabin");
  const int64_t answer = gguf_find_key(in.get(), "test.answer");
  CHECK(answer >= 0 && gguf_get_val_u32(in.get(), answer) == 42);
  CHECK(gguf_find_key(in.get(), "missing") == -1);
  CHECK(gguf_get_n_tensors(in.get()) == 1 && gguf_get_tensor_type(in.get(), 0) == GGML_TYPE_F32);
  const ggml_tensor *back = ggml_get_tensor(loaded, "weights");
  CHECK(back != nullptr && back->ne[0] == 4 && std::memcmp(back->data, values, sizeof values) == 0);
}

}  // namespace

int main() {
  ggml_backend_dev_t dev = test_registry();
  ggml_backend_ptr backend(ggml_backend_init_by_type(GGML_BACKEND_DEVICE_TYPE_CPU, nullptr));
  CHECK(backend != nullptr && ggml_backend_is_cpu(backend.get()));
  CHECK(backend && std::string(ggml_backend_name(backend.get())) == "CPU");
  if (dev && backend) {
    ggml_backend_cpu_set_n_threads(backend.get(), 3);
    test_mul_mat(backend.get());
    test_repack(dev, backend.get());
  }
  test_gguf();
  std::printf("ggml (C++): %s\n", failures == 0 ? "ok" : "FAILED");
  return failures == 0 ? 0 : 1;
}
