// The C++ consumer, on the architecture the compiler target and flags
// select (xsimd::default_arch, such as SSE2 on baseline x86-64 or NEON64
// on arm64): lane arithmetic, aligned and unaligned loads, stores,
// reductions, masks and select, math functions, integer bit operations
// and casts, the README's mean with its scalar tail, and runtime dispatch.
#include <xsimd/xsimd.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

#define CHECK(cond)                                                    \
  do {                                                                 \
    if (!(cond)) {                                                     \
      std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__,      \
                   __LINE__, #cond);                                   \
      std::exit(1);                                                    \
    }                                                                  \
  } while (0)

using arch = xsimd::default_arch;
using fbatch = xsimd::batch<float>;
using dbatch = xsimd::batch<double>;
using ibatch = xsimd::batch<std::int32_t>;
template <class T>
using aligned_vector = std::vector<T, xsimd::aligned_allocator<T>>;

// The README's mean of two vectors: whole batches, then a scalar tail.
static void mean(const aligned_vector<double>& a,
                 const aligned_vector<double>& b, aligned_vector<double>& res) {
  std::size_t size = a.size();
  constexpr std::size_t simd_size = dbatch::size;
  std::size_t vec_size = size - size % simd_size;
  for (std::size_t i = 0; i < vec_size; i += simd_size) {
    auto ba = xsimd::load_aligned(&a[i]);
    auto bb = xsimd::load_aligned(&b[i]);
    auto bres = (ba + bb) / 2.;
    bres.store_aligned(&res[i]);
  }
  for (std::size_t i = vec_size; i < size; ++i) res[i] = (a[i] + b[i]) / 2.;
}

// A sum over the best architecture the running CPU supports among those
// the compiler targets.
struct sum {
  template <class Arch>
  float operator()(Arch, const float* data, std::size_t n) const {
    using batch = xsimd::batch<float, Arch>;
    batch acc(0.f);
    std::size_t i = 0;
    for (; i + batch::size <= n; i += batch::size)
      acc += batch::load_unaligned(data + i);
    float s = xsimd::reduce_add(acc);
    for (; i < n; ++i) s += data[i];
    return s;
  }
};

static bool near(double got, double want) {
  return std::fabs(got - want) <= 1e-12 * (1 + std::fabs(want));
}

int main() {
  static_assert(fbatch::size >= 4 && dbatch::size >= 2 &&
                    ibatch::size == fbatch::size,
                "unexpected batch sizes");
  // The compile-time architecture is one this CPU runs.
  CHECK(xsimd::available_architectures().has(arch{}));

  // Lane arithmetic on an aligned and an unaligned load; the values are
  // exact in float, so every lane matches the scalar result exactly.
  constexpr std::size_t n = fbatch::size;
  aligned_vector<float> x(n), y(n + 1), out(n);
  CHECK(xsimd::is_aligned(x.data()) && xsimd::is_aligned(out.data()));
  for (std::size_t i = 0; i < n; ++i) x[i] = float(i + 1);
  for (std::size_t i = 0; i <= n; ++i) y[i] = 0.5f * float(i);
  fbatch a = fbatch::load_aligned(x.data());
  fbatch b = fbatch::load_unaligned(y.data() + 1);
  ((a * b + a) / fbatch(2.f) - b).store_aligned(out.data());
  for (std::size_t i = 0; i < n; ++i)
    CHECK(out[i] == (x[i] * y[i + 1] + x[i]) / 2.f - y[i + 1]);

  CHECK(xsimd::reduce_add(a) == float(n * (n + 1) / 2));
  CHECK(xsimd::reduce_max(a) == float(n) && xsimd::reduce_min(a) == 1.f);

  // Masks: lanes 0 and 1 (values 1 and 2) fail a > 2.5.
  auto mask = a > fbatch(2.5f);
  CHECK(xsimd::count(mask) == n - 2);
  CHECK(xsimd::any(mask) && !xsimd::all(mask));
  CHECK(xsimd::none(a < fbatch(0.f)));
  xsimd::select(mask, a, fbatch(-1.f)).store_aligned(out.data());
  for (std::size_t i = 0; i < n; ++i)
    CHECK(out[i] == (i < 2 ? -1.f : float(i + 1)));

  // Math functions against the C library.
  aligned_vector<double> d(dbatch::size), r(dbatch::size);
  for (std::size_t i = 0; i < d.size(); ++i) d[i] = 0.25 + 0.5 * double(i);
  dbatch v = dbatch::load_aligned(d.data());
  xsimd::sqrt(v).store_aligned(r.data());
  for (std::size_t i = 0; i < d.size(); ++i) CHECK(near(r[i], std::sqrt(d[i])));
  xsimd::exp(v).store_aligned(r.data());
  for (std::size_t i = 0; i < d.size(); ++i) CHECK(near(r[i], std::exp(d[i])));
  xsimd::sin(v).store_aligned(r.data());
  for (std::size_t i = 0; i < d.size(); ++i) CHECK(near(r[i], std::sin(d[i])));
  xsimd::log(v).store_aligned(r.data());
  for (std::size_t i = 0; i < d.size(); ++i) CHECK(near(r[i], std::log(d[i])));
  xsimd::fma(v, v, v).store_aligned(r.data());
  for (std::size_t i = 0; i < d.size(); ++i)
    CHECK(near(r[i], d[i] * d[i] + d[i]));

  // Integer bit operations, shifts (arithmetic for negative lanes), and
  // conversions.
  ibatch bits(0x0F0F);
  CHECK(xsimd::all((bits << 4) == ibatch(0xF0F0)));
  CHECK(xsimd::all((bits >> 8) == ibatch(0x0F)));
  CHECK(xsimd::all((bits & ibatch(0xFF)) == ibatch(0x0F)));
  CHECK(xsimd::all((bits ^ bits) == ibatch(0)));
  CHECK(xsimd::all((ibatch(-16) >> 2) == ibatch(-4)));
  aligned_vector<std::int32_t> ints(n), iout(n);
  for (std::size_t i = 0; i < n; ++i) ints[i] = std::int32_t(3 * i) - 5;
  ibatch iv = ibatch::load_aligned(ints.data());
  xsimd::abs(iv).store_aligned(iout.data());
  for (std::size_t i = 0; i < n; ++i) CHECK(iout[i] == std::abs(ints[i]));
  xsimd::batch_cast<float>(iv).store_aligned(out.data());
  for (std::size_t i = 0; i < n; ++i) CHECK(out[i] == float(ints[i]));

  // The README's mean, over a length that leaves a scalar tail.
  std::size_t len = 3 * dbatch::size + 1;
  aligned_vector<double> ma(len), mb(len), mres(len);
  for (std::size_t i = 0; i < len; ++i) {
    ma[i] = double(i);
    mb[i] = 2.0 * double(i) + 1.0;
  }
  mean(ma, mb, mres);
  for (std::size_t i = 0; i < len; ++i)
    CHECK(mres[i] == (3.0 * double(i) + 1.0) / 2.0);

  // Runtime dispatch; every partial sum is an integer below 2^24, so the
  // result is exact.
  std::vector<float> data(1003);
  long expected = 0;
  for (std::size_t i = 0; i < data.size(); ++i) {
    data[i] = float(i % 7);
    expected += long(i % 7);
  }
  CHECK(xsimd::dispatch(sum{})(data.data(), data.size()) == float(expected));

  std::puts("ok");
  return 0;
}
