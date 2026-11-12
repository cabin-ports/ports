// C consumer: builds graphs in a ggml context, runs them on the CPU backend,
// and checks the values against sums computed here.
#include <ggml.h>
#include <ggml-cpu.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond)                                                   \
  do {                                                                \
    if (!(cond)) {                                                    \
      printf("main.c:%d: check failed: %s\n", __LINE__, #cond);       \
      ++failures;                                                     \
    }                                                                 \
  } while (0)

static int near(double a, double b, double tol) { return fabs(a - b) <= tol; }

static struct ggml_context *new_context(void) {
  struct ggml_init_params params = {16 * 1024 * 1024, NULL, false};
  return ggml_init(params);
}

static void set_rows(struct ggml_tensor *t, const float *values) {
  memcpy(t->data, values, ggml_nbytes(t));
}

// IEEE 754 half precision: exact values and their bit patterns.
static void test_fp16(void) {
  CHECK(ggml_fp32_to_fp16(1.0f) == 0x3C00);
  CHECK(ggml_fp32_to_fp16(1.5f) == 0x3E00);
  CHECK(ggml_fp32_to_fp16(-2.0f) == 0xC000);
  CHECK(ggml_fp32_to_fp16(65504.0f) == 0x7BFF);
  CHECK(ggml_fp16_to_fp32(0x3555) == 0.333251953125f);
}

// relu(a x b^T + bias), then a row-wise softmax and a total.
static void test_graph(void) {
  struct ggml_context *ctx = new_context();
  CHECK(ctx != NULL);
  if (!ctx) return;

  // ggml shapes list the contiguous dimension first: 3 columns, 2 rows.
  struct ggml_tensor *a = ggml_new_tensor_2d(ctx, GGML_TYPE_F32, 3, 2);
  struct ggml_tensor *b = ggml_new_tensor_2d(ctx, GGML_TYPE_F32, 3, 2);
  struct ggml_tensor *bias = ggml_new_tensor_1d(ctx, GGML_TYPE_F32, 2);
  const float av[] = {1, 2, 3, 4, 5, 6};
  const float bv[] = {1, 0, -1, 2, 1, 0};
  const float biasv[] = {1, -3};
  set_rows(a, av);
  set_rows(b, bv);
  set_rows(bias, biasv);

  // Row j of the product holds the dot products of b's row j with a's rows:
  // {-2, -2} and {4, 13}; the bias is broadcast along each row.
  struct ggml_tensor *act = ggml_relu(ctx, ggml_add(ctx, ggml_mul_mat(ctx, a, b), bias));
  struct ggml_tensor *probs = ggml_soft_max(ctx, act);
  struct ggml_tensor *total = ggml_sum(ctx, act);

  struct ggml_cgraph *gf = ggml_new_graph(ctx);
  ggml_build_forward_expand(gf, probs);
  ggml_build_forward_expand(gf, total);
  CHECK(ggml_graph_compute_with_ctx(ctx, gf, 2) == GGML_STATUS_SUCCESS);

  CHECK(act->ne[0] == 2 && act->ne[1] == 2);
  const float *y = ggml_get_data_f32(act);
  CHECK(y[0] == 0 && y[1] == 0 && y[2] == 5 && y[3] == 10);
  const float *p = ggml_get_data_f32(probs);
  const double e5 = exp(5.0);
  CHECK(near(p[0], 0.5, 1e-6) && near(p[1], 0.5, 1e-6));
  CHECK(near(p[2], 1 / (1 + e5), 1e-6) && near(p[3], e5 / (1 + e5), 1e-6));
  CHECK(ggml_get_f32_1d(total, 0) == 15);

  ggml_free(ctx);
}

// A Q4_0 weight matrix times an F32 input: the CPU backend quantizes the
// input to Q8_0 and runs the architecture's dot-product kernel.
static void test_quantized(void) {
  enum { K = 64, M = 4, N = 3 };
  static float w[M * K], x[N * K];
  for (int i = 0; i < M * K; ++i) w[i] = (float)sin(0.37 * i);
  for (int i = 0; i < N * K; ++i) x[i] = (float)cos(0.11 * i) * 0.5f;

  struct ggml_context *ctx = new_context();
  CHECK(ctx != NULL);
  if (!ctx) return;
  struct ggml_tensor *wq = ggml_new_tensor_2d(ctx, GGML_TYPE_Q4_0, K, M);
  struct ggml_tensor *xt = ggml_new_tensor_2d(ctx, GGML_TYPE_F32, K, N);
  set_rows(xt, x);

  // A Q4_0 block is an fp16 scale and 32 four-bit values: 18 bytes.
  CHECK(ggml_blck_size(GGML_TYPE_Q4_0) == 32);
  CHECK(ggml_row_size(GGML_TYPE_Q4_0, K) == 36);
  CHECK(ggml_quantize_chunk(GGML_TYPE_Q4_0, w, wq->data, 0, M, K, NULL) == M * 36);

  struct ggml_tensor *y = ggml_mul_mat(ctx, wq, xt);
  struct ggml_cgraph *gf = ggml_new_graph(ctx);
  ggml_build_forward_expand(gf, y);
  CHECK(ggml_graph_compute_with_ctx(ctx, gf, 2) == GGML_STATUS_SUCCESS);

  // Reference: the dequantized weights times the input after the same
  // Q8_0 rounding, summed in double; and the unquantized product.
  const struct ggml_type_traits *q4 = ggml_get_type_traits(GGML_TYPE_Q4_0);
  const struct ggml_type_traits *q8 = ggml_get_type_traits(GGML_TYPE_Q8_0);
  CHECK(q4->is_quantized && q8->is_quantized);
  static float wd[M * K], xd[K];
  q4->to_float(wq->data, wd, M * K);
  // One Q8_0 row, in storage aligned for its block structs.
  void *xq = malloc(ggml_row_size(GGML_TYPE_Q8_0, K));
  CHECK(xq != NULL);
  if (!xq) {
    ggml_free(ctx);
    return;
  }
  const float *out = ggml_get_data_f32(y);
  for (int n = 0; n < N; ++n) {
    q8->from_float_ref(x + n * K, xq, K);
    q8->to_float(xq, xd, K);
    for (int m = 0; m < M; ++m) {
      double dq = 0, exact = 0;
      for (int k = 0; k < K; ++k) {
        dq += (double)wd[m * K + k] * xd[k];
        exact += (double)w[m * K + k] * x[n * K + k];
      }
      CHECK(near(out[n * M + m], dq, 0.02));
      CHECK(near(out[n * M + m], exact, 0.5));
    }
  }

  free(xq);
  ggml_free(ctx);
}

int main(void) {
  CHECK(strcmp(ggml_version(), "0.26.0") == 0);
  CHECK(strcmp(ggml_commit(), "unknown") == 0);
  test_fp16();
  test_graph();
  test_quantized();
  printf("ggml (C): %s\n", failures == 0 ? "ok" : "FAILED");
  return failures == 0 ? 0 : 1;
}
