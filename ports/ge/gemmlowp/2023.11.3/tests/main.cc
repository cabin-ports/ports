#include <cstdint>
#include <cstdio>
#include <tuple>
#include <vector>

#include "eight_bit_int_gemm/eight_bit_int_gemm.h"
#include "fixedpoint/fixedpoint.h"
#include "public/gemmlowp.h"

static int failures = 0;

#define CHECK_EQ(a, b)                                                     \
  do {                                                                     \
    long long va = (a), vb = (b);                                          \
    if (va != vb) {                                                        \
      std::printf("%s:%d: %s = %lld, expected %lld\n", __FILE__, __LINE__, \
                  #a, va, vb);                                             \
      ++failures;                                                          \
    }                                                                      \
  } while (0)

// [1 2 3; 4 5 6] * [7 8; 9 10; 11 12] = [58 64; 139 154]
static void SmallGemm(int lhs_offset, const std::int32_t expected[4]) {
  const std::uint8_t lhs[6] = {1, 2, 3, 4, 5, 6};
  const std::uint8_t rhs[6] = {7, 8, 9, 10, 11, 12};
  gemmlowp::MatrixMap<const std::uint8_t, gemmlowp::MapOrder::RowMajor> L(lhs, 2, 3);
  gemmlowp::MatrixMap<const std::uint8_t, gemmlowp::MapOrder::RowMajor> R(rhs, 3, 2);
  std::int32_t out[4] = {};
  gemmlowp::MatrixMap<std::int32_t, gemmlowp::MapOrder::RowMajor> O(out, 2, 2);
  gemmlowp::GemmContext ctx;
  gemmlowp::GemmWithOutputPipeline<std::uint8_t, std::int32_t,
                                   gemmlowp::DefaultL8R8BitDepthParams>(
      &ctx, L, R, &O, lhs_offset, 0, std::make_tuple());
  for (int i = 0; i < 4; ++i) CHECK_EQ(out[i], expected[i]);
}

// Large enough to be split across the worker pool; every entry is 2 * 3 * k.
static void ThreadedGemm() {
  const int m = 40, n = 50, k = 70;
  std::vector<std::uint8_t> lhs(m * k, 2), rhs(k * n, 3);
  std::vector<std::int32_t> out(m * n, 0);
  gemmlowp::MatrixMap<const std::uint8_t, gemmlowp::MapOrder::RowMajor> L(lhs.data(), m, k);
  gemmlowp::MatrixMap<const std::uint8_t, gemmlowp::MapOrder::RowMajor> R(rhs.data(), k, n);
  gemmlowp::MatrixMap<std::int32_t, gemmlowp::MapOrder::RowMajor> O(out.data(), m, n);
  gemmlowp::GemmContext ctx;
  ctx.set_max_num_threads(4);
  gemmlowp::GemmWithOutputPipeline<std::uint8_t, std::int32_t,
                                   gemmlowp::DefaultL8R8BitDepthParams>(
      &ctx, L, R, &O, 0, 0, std::make_tuple());
  for (int i = 0; i < m * n; ++i) CHECK_EQ(out[i], 6 * k);
}

// Column-major operands; the result is clamped to uint8 after c_mult_int.
static void EightBitEntry() {
  const std::uint8_t a[6] = {1, 4, 2, 5, 3, 6};
  const std::uint8_t b[6] = {7, 9, 11, 8, 10, 12};
  std::uint8_t c[4] = {};
  gemmlowp::eight_bit_int_gemm::EightBitIntGemm(
      false, false, false, 2, 2, 3, a, 0, 2, b, 0, 3, c, 0, 1, 0, 2,
      gemmlowp::eight_bit_int_gemm::BitDepthSetting::A8B8);
  const int plain[4] = {58, 139, 64, 154};
  for (int i = 0; i < 4; ++i) CHECK_EQ(c[i], plain[i]);
  gemmlowp::eight_bit_int_gemm::EightBitIntGemm(
      false, false, false, 2, 2, 3, a, 0, 2, b, 0, 3, c, 0, 2, 0, 2,
      gemmlowp::eight_bit_int_gemm::BitDepthSetting::A8B8);
  const int doubled[4] = {116, 255, 128, 255};
  for (int i = 0; i < 4; ++i) CHECK_EQ(c[i], doubled[i]);
  gemmlowp::eight_bit_int_gemm::FreePersistentResources();
}

static void FixedPoint() {
  const std::int32_t half = std::int32_t(1) << 30;
  CHECK_EQ(gemmlowp::SaturatingRoundingDoublingHighMul(half, half), 536870912);
  CHECK_EQ(gemmlowp::SaturatingRoundingDoublingHighMul(INT32_MIN, INT32_MIN),
           INT32_MAX);
  CHECK_EQ(gemmlowp::RoundingDivideByPOT(std::int32_t(5), 1), 3);
  CHECK_EQ(gemmlowp::RoundingDivideByPOT(std::int32_t(-5), 1), -3);
}

int main() {
  const std::int32_t plain[4] = {58, 64, 139, 154};
  const std::int32_t shifted[4] = {31, 34, 112, 124};  // lhs_offset = -1
  SmallGemm(0, plain);
  SmallGemm(-1, shifted);
  ThreadedGemm();
  EightBitEntry();
  FixedPoint();
  return failures != 0;
}
