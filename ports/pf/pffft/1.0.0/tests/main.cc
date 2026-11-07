// pffft from C++98: a complex impulse transformed out of place and in
// place, and the inverse transform back to N times the impulse.
#include <cmath>
#include <cstdio>
#include <cstring>

#include <pffft.h>

namespace {

int failures = 0;

void check(bool ok, int line, const char *what) {
  if (!ok) {
    std::fprintf(stderr, "main.cc:%d: check failed: %s\n", line, what);
    ++failures;
  }
}

#define CHECK(cond) check((cond), __LINE__, #cond)

const double kPi = 3.14159265358979323846;

// pffft wants SIMD-aligned buffers.
class Floats {
 public:
  explicit Floats(int count)
      : data_(static_cast<float *>(pffft_aligned_malloc(count * sizeof(float)))) {
    std::memset(data_, 0, count * sizeof(float));
  }
  ~Floats() { pffft_aligned_free(data_); }
  float &operator[](int i) { return data_[i]; }
  float *get() { return data_; }

 private:
  Floats(const Floats &);
  Floats &operator=(const Floats &);
  float *data_;
};

bool near(float actual, double expected, double tolerance) {
  if (std::fabs(actual - expected) <= tolerance) {
    return true;
  }
  std::fprintf(stderr, "  %g, expected %g\n", actual, expected);
  return false;
}

}  // namespace

int main() {
  // A unit impulse at n = 1 has X[k] = e^(-2 pi i k/N) under the forward
  // transform.
  const int N = 32;
  PFFFT_Setup *setup = pffft_new_setup(N, PFFFT_COMPLEX);
  CHECK(setup != NULL);
  Floats x(2 * N), spectrum(2 * N), back(2 * N);
  x[2] = 1;

  pffft_transform_ordered(setup, x.get(), spectrum.get(), NULL, PFFFT_FORWARD);
  for (int k = 0; k < N; ++k) {
    CHECK(near(spectrum[2 * k], std::cos(2 * kPi * k / N), 1e-5));
    CHECK(near(spectrum[2 * k + 1], -std::sin(2 * kPi * k / N), 1e-5));
  }

  // Input and output may alias.
  Floats in_place(2 * N);
  std::memcpy(in_place.get(), x.get(), 2 * N * sizeof(float));
  pffft_transform_ordered(setup, in_place.get(), in_place.get(), NULL,
                          PFFFT_FORWARD);
  CHECK(std::memcmp(in_place.get(), spectrum.get(), 2 * N * sizeof(float)) ==
        0);

  // Unscaled: the inverse yields N times the impulse.
  pffft_transform_ordered(setup, spectrum.get(), back.get(), NULL,
                          PFFFT_BACKWARD);
  for (int n = 0; n < 2 * N; ++n) {
    CHECK(near(back[n], n == 2 ? N : 0, 1e-4));
  }

  pffft_destroy_setup(setup);
  if (failures != 0) {
    return 1;
  }
  std::printf("pffft (SIMD width %d): C++ API OK\n", pffft_simd_size());
  return 0;
}
