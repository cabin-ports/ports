// From C++, fxdiv splits flat indices back into coordinates the way thread
// pools use it, rounds down to a granularity, and its three divisor families
// agree with / and % on std::mt19937_64 draws of every bit width.
#include <fxdiv.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <random>

namespace {

int failures = 0;

void check(bool ok, int line, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "main.cc:%d: check failed: %s\n", line, what);
    ++failures;
  }
}

#define CHECK(expr) check((expr), __LINE__, #expr)

template <typename T, typename Divisor, typename Result>
struct family {
  Divisor (*init)(T);
  T (*quotient)(T, Divisor);
  T (*remainder)(T, Divisor);
  T (*round_down)(T, Divisor);
  Result (*divide)(T, Divisor);
};

template <typename T, typename Divisor, typename Result>
int mismatches(const family<T, Divisor, Result>& f, std::mt19937_64& engine) {
  int wrong = 0;
  for (int i = 0; i < 100000; ++i) {
    const T n = static_cast<T>(engine());
    const unsigned shift = static_cast<unsigned>(engine() % 64);
    T d = static_cast<T>(engine() >> shift);
    if (d == 0) {
      d = 1;
    }
    const Divisor divisor = f.init(d);
    const Result result = f.divide(n, divisor);
    if (f.quotient(n, divisor) != n / d || f.remainder(n, divisor) != n % d ||
        result.quotient != n / d || result.remainder != n % d ||
        f.round_down(n, divisor) != n - n % d) {
      ++wrong;
    }
  }
  return wrong;
}

}  // namespace

int main() {
  // A rows x cols x depth range walked by one flat index.
  const std::size_t rows = 37, cols = 129, depth = 7;
  const fxdiv_divisor_size_t cols_divisor = fxdiv_init_size_t(cols);
  const fxdiv_divisor_size_t depth_divisor = fxdiv_init_size_t(depth);
  std::size_t index = 0;
  int wrong = 0;
  for (std::size_t r = 0; r < rows; ++r) {
    for (std::size_t c = 0; c < cols; ++c) {
      for (std::size_t k = 0; k < depth; ++k, ++index) {
        const fxdiv_result_size_t ck = fxdiv_divide_size_t(index, depth_divisor);
        const fxdiv_result_size_t rc = fxdiv_divide_size_t(ck.quotient, cols_divisor);
        if (rc.quotient != r || rc.remainder != c || ck.remainder != k) {
          ++wrong;
        }
      }
    }
  }
  CHECK(wrong == 0);
  CHECK(fxdiv_round_down_size_t(1000, fxdiv_init_size_t(64)) == 960);
  CHECK(fxdiv_round_down_uint64_t(UINT64_MAX, fxdiv_init_uint64_t(UINT64_C(1) << 40)) ==
        UINT64_MAX - ((UINT64_C(1) << 40) - 1));

  std::mt19937_64 engine(20201209);
  const family<std::uint32_t, fxdiv_divisor_uint32_t, fxdiv_result_uint32_t> u32 = {
      fxdiv_init_uint32_t, fxdiv_quotient_uint32_t, fxdiv_remainder_uint32_t,
      fxdiv_round_down_uint32_t, fxdiv_divide_uint32_t};
  const family<std::uint64_t, fxdiv_divisor_uint64_t, fxdiv_result_uint64_t> u64 = {
      fxdiv_init_uint64_t, fxdiv_quotient_uint64_t, fxdiv_remainder_uint64_t,
      fxdiv_round_down_uint64_t, fxdiv_divide_uint64_t};
  const family<std::size_t, fxdiv_divisor_size_t, fxdiv_result_size_t> size = {
      fxdiv_init_size_t, fxdiv_quotient_size_t, fxdiv_remainder_size_t, fxdiv_round_down_size_t,
      fxdiv_divide_size_t};
  CHECK(mismatches(u32, engine) == 0);
  CHECK(mismatches(u64, engine) == 0);
  CHECK(mismatches(size, engine) == 0);

  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
#if defined(_MSC_VER) && _MSC_VER >= 1920 && defined(_M_X64) && !defined(__clang__) && !defined(__INTEL_COMPILER)
  std::printf("built with MSVC x64's __umulh, _BitScanReverse64 and _udiv128 branches (_MSC_VER %d)\n",
              _MSC_VER);
#endif
  std::puts("fxdiv C++ consumer test passed");
  return 0;
}
