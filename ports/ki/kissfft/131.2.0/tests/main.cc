// Consumer: the C++ template kissfft.hh in float and double, complex and
// real, against a direct DFT in long double, and the C library through its
// C++-compatible headers.
#include <kissfft.hh>
#include <kissfft/kiss_fftr.h>

#include <cmath>
#include <complex>
#include <cstdio>
#include <vector>

namespace {

int failures = 0;

#define CHECK(cond)                                                   \
  do {                                                                \
    if (!(cond)) {                                                    \
      std::printf("main.cc:%d: check failed: %s\n", __LINE__, #cond); \
      ++failures;                                                     \
    }                                                                 \
  } while (0)

typedef std::complex<long double> lcpx;

// Direct DFT; kissfft's forward transform uses exp(-2 pi i jk/n), its
// inverse exp(+2 pi i jk/n), neither scaled.
std::vector<lcpx> dft(const std::vector<lcpx>& x, bool inverse) {
  const std::size_t n = x.size();
  const long double two_pi = 6.283185307179586476925286766559L;
  std::vector<lcpx> out(n);
  for (std::size_t k = 0; k < n; ++k)
    for (std::size_t j = 0; j < n; ++j)
      out[k] += x[j] * std::polar(1.0L, (inverse ? two_pi : -two_pi) * static_cast<long double>(j * k % n) / n);
  return out;
}

// The larger of two errors; a NaN wins and stays, so its check fails.
long double worse(long double err, long double d) { return (err != err || err >= d) ? err : d; }

long double input(std::size_t i, int part) { return std::sin(0.37L * i + 1.3L * part) + 0.25L * std::cos(1.9L * i); }

template <typename T>
void check_template(std::size_t n, long double tolerance) {
  typedef std::complex<T> cpx;
  const int before = failures;
  std::vector<cpx> in(n), out(n);
  std::vector<lcpx> ref(n);
  for (std::size_t i = 0; i < n; ++i) {
    in[i] = cpx(static_cast<T>(input(i, 0)), static_cast<T>(input(i, 1)));
    ref[i] = lcpx(in[i].real(), in[i].imag());
  }
  for (int inverse = 0; inverse < 2; ++inverse) {
    const kissfft<T> fft(n, inverse != 0);
    fft.transform(&in[0], &out[0]);
    const std::vector<lcpx> expect = dft(ref, inverse != 0);
    long double err = 0;
    for (std::size_t k = 0; k < n; ++k)
      err = worse(err, std::abs(lcpx(out[k].real(), out[k].imag()) - expect[k]));
    CHECK(err <= tolerance * n);
  }

  // assign() to another length builds the object afresh, so it can change
  // the direction too; with the length unchanged it cannot (see cabin.toml).
  kissfft<T> fft(n, false);
  fft.assign(2 * n, false);
  std::vector<cpx> ones(2 * n, cpx(1, 0)), spectrum(2 * n);
  fft.transform(&ones[0], &spectrum[0]);
  CHECK(std::abs(spectrum[0] - cpx(static_cast<T>(2 * n), 0)) <= tolerance * n && std::abs(spectrum[1]) <= tolerance * n);
  fft.assign(n, true);
  std::vector<cpx> a(n), b(n);
  fft.transform(&in[0], &a[0]);
  kissfft<T>(n, true).transform(&in[0], &b[0]);
  CHECK(a == b);

  // transform_real: 2n real samples, n bins; bin 0 packs DC and Nyquist.
  {
    std::vector<T> real(2 * n);
    std::vector<lcpx> real_ref(2 * n);
    for (std::size_t i = 0; i < 2 * n; ++i) {
      real[i] = static_cast<T>(input(i, 2));
      real_ref[i] = real[i];
    }
    const std::vector<lcpx> expect = dft(real_ref, false);
    std::vector<cpx> half(n);
    kissfft<T>(n, false).transform_real(&real[0], &half[0]);
    long double err = worse(std::abs(half[0].real() - expect[0].real()), std::abs(half[0].imag() - expect[n].real()));
    for (std::size_t k = 1; k < n; ++k)
      err = worse(err, std::abs(lcpx(half[k].real(), half[k].imag()) - expect[k]));
    CHECK(err <= tolerance * 2 * n);
  }
  if (failures != before) std::printf("  kissfft<%s> n=%u\n", sizeof(T) == sizeof(float) ? "float" : "double", unsigned(n));
}

}  // namespace

int main() {
  static const std::size_t sizes[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 12, 15, 16, 25, 30, 49, 60, 64, 77, 97, 120, 256};
  for (std::size_t i = 0; i < sizeof sizes / sizeof sizes[0]; ++i) {
    check_template<float>(sizes[i], 2e-6L);
    check_template<double>(sizes[i], 1e-14L);
  }

  // The C library, float as configured, from C++.
  const int n = 60;
  kiss_fftr_cfg cfg = kiss_fftr_alloc(n, 0, NULL, NULL);
  CHECK(cfg != NULL);
  float x[n];
  kiss_fft_cpx y[n / 2 + 1];
  for (int i = 0; i < n; ++i) x[i] = static_cast<float>(std::cos(6.283185307179586 * 7 * i / n));
  kiss_fftr(cfg, x, y);
  // A cosine at bin 7 puts n/2 there and nothing elsewhere.
  for (int k = 0; k <= n / 2; ++k)
    CHECK(std::fabs(y[k].r - (k == 7 ? n / 2.0 : 0.0)) < 1e-4 && std::fabs(y[k].i) < 1e-4);
  kiss_fftr_free(cfg);
  CHECK(kiss_fft_next_fast_size(61) == 64);

  std::printf("kissfft: %s\n", failures == 0 ? "ok" : "FAILED");
  return failures == 0 ? 0 : 1;
}
