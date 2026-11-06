// As hwy/per_target.cc does, so that this TU's HWY_TARGETS matches the
// targets the library generates, including the portable fallback every
// CPU supports (EMU128, or SCALAR under GCC before 16).
#define HWY_COMPILE_ALL_ATTAINABLE

#include <stdint.h>
#include <stdio.h>

#include "hwy/aligned_allocator.h"
#include "hwy/highway.h"
#include "hwy/per_target.h"
#include "hwy/targets.h"

static int failures = 0;

static void check(bool ok, const char* expr, int line) {
  if (!ok) {
    fprintf(stderr, "main.cc:%d: CHECK(%s) failed\n", line, expr);
    ++failures;
  }
}

#define CHECK(...) check((__VA_ARGS__), #__VA_ARGS__, __LINE__)

// Vector code for the static target, the best one this compile's
// baseline flags allow.
HWY_BEFORE_NAMESPACE();
namespace probe {
namespace HWY_NAMESPACE {
namespace hn = hwy::HWY_NAMESPACE;

size_t VectorBytes() { return hn::Lanes(hn::ScalableTag<uint8_t>()); }

// out[i] = a[i] * b[i] + c[i], whole vectors then a scalar tail; returns
// the sum of out.
int64_t MulAdd(const int32_t* HWY_RESTRICT a, const int32_t* HWY_RESTRICT b,
               const int32_t* HWY_RESTRICT c, int32_t* HWY_RESTRICT out,
               size_t n) {
  const hn::ScalableTag<int32_t> d;
  const size_t lanes = hn::Lanes(d);
  int64_t sum = 0;
  size_t i = 0;
  for (; i + lanes <= n; i += lanes) {
    const auto v = hn::Add(hn::Mul(hn::LoadU(d, a + i), hn::LoadU(d, b + i)),
                           hn::LoadU(d, c + i));
    hn::StoreU(v, d, out + i);
    sum += hn::ReduceSum(d, v);
  }
  for (; i < n; ++i) {
    out[i] = a[i] * b[i] + c[i];
    sum += out[i];
  }
  return sum;
}

}  // namespace HWY_NAMESPACE
}  // namespace probe
HWY_AFTER_NAMESPACE();

static void CheckStaticTarget() {
  const size_t n = 1003;  // not a multiple of any vector length
  auto a = hwy::AllocateAligned<int32_t>(n);
  auto b = hwy::AllocateAligned<int32_t>(n);
  auto c = hwy::AllocateAligned<int32_t>(n);
  auto out = hwy::AllocateAligned<int32_t>(n);
  CHECK(a && b && c && out);
  if (!(a && b && c && out)) return;
  CHECK(reinterpret_cast<uintptr_t>(a.get()) % HWY_ALIGNMENT == 0);

  int64_t want_sum = 0;
  for (size_t i = 0; i < n; ++i) {
    a[i] = static_cast<int32_t>(i % 97) - 48;
    b[i] = static_cast<int32_t>(i % 13) + 1;
    c[i] = 7 - static_cast<int32_t>(i % 5);
    out[i] = 0;
    want_sum += a[i] * b[i] + c[i];
  }
  CHECK(probe::HWY_NAMESPACE::MulAdd(a.get(), b.get(), c.get(), out.get(),
                                     n) == want_sum);
  size_t wrong = 0;
  for (size_t i = 0; i < n; ++i) wrong += out[i] != a[i] * b[i] + c[i];
  CHECK(wrong == 0);
}

// hwy/per_target.cc is compiled once per generated target through
// foreach_target.h and dispatched at run time; force each target the CPU
// supports in turn and check that the library dispatches to it.
static size_t CheckLibraryDispatch() {
  size_t tested = 0;
  bool saw_static = false;
  for (int64_t target : hwy::SupportedAndGeneratedTargets()) {
    hwy::SetSupportedTargetsForTest(target);
    CHECK(hwy::DispatchedTarget() == target);
    const size_t bytes = hwy::VectorBytes();
    CHECK(bytes != 0 && (bytes & (bytes - 1)) == 0);
    if (target == HWY_STATIC_TARGET) {
      saw_static = true;
      CHECK(bytes == probe::HWY_NAMESPACE::VectorBytes());
    }
    printf("%s: %zu-byte vectors\n", hwy::TargetName(target), bytes);
    ++tested;
  }
  hwy::SetSupportedTargetsForTest(0);
  CHECK(saw_static);
  return tested;
}

int main() {
  CheckStaticTarget();
  const size_t tested = CheckLibraryDispatch();
#if HWY_ARCH_X86_64 || HWY_ARCH_ARM_A64
  // SSE2 or NEON is the baseline there, besides the portable fallback.
  CHECK(tested >= 2);
#else
  CHECK(tested >= 1);
#endif
  if (failures != 0) {
    fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  puts("ok");
  return 0;
}
