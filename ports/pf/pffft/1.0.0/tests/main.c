/* pffft from C: real and complex FFTs of signals with known spectra,
   in canonical and internal order, inverse transforms, FFT convolution
   against a direct one, and the setup and buffer rules of pffft.h. */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <pffft.h>

static int failures = 0;

static void check(int ok, int line, const char *what) {
  if (!ok) {
    fprintf(stderr, "main.c:%d: check failed: %s\n", line, what);
    ++failures;
  }
}

#define CHECK(cond) check((cond) != 0, __LINE__, #cond)

static const double kPi = 3.14159265358979323846;

/* Whether |a[i] - b[i]| <= tolerance for every i. */
static int near_all(const float *a, const float *b, int count,
                    float tolerance) {
  int i;
  for (i = 0; i < count; ++i) {
    if (!(fabsf(a[i] - b[i]) <= tolerance)) {
      fprintf(stderr, "  [%d]: %g, expected %g\n", i, a[i], b[i]);
      return 0;
    }
  }
  return 1;
}

static float *floats(int count) {
  float *p = (float *)pffft_aligned_malloc(count * sizeof(float));
  memset(p, 0, count * sizeof(float));
  return p;
}

static void check_real(void) {
  /* x[n] = 1 + 2 cos(2 pi 5n/N) + sin(2 pi 3n/N) + 0.5 (-1)^n
            + 0.75 sin(2 pi 13n/N) + 1.5 cos(2 pi 20n/N) - sin(2 pi 27n/N).
     With the forward transform X[k] = sum x[n] e^(-2 pi i kn/N)
     (fftpack.h:205-211), its spectrum is X[0] = N, X[5] = N, X[3] = -iN/2,
     X[N/2] = N/2, X[13] = -3iN/8, X[20] = 3N/4 and X[27] = iN/2: energy
     in each quarter of the spectrum, which pffft_zreorder places
     separately (pffft.c:1331-1337).  The canonical real order holds X[0]
     and X[N/2] first, then (Re, Im) of X[1] .. X[N/2-1]
     (pffft.h:139-146). */
  enum { N = 64 };
  PFFFT_Setup *setup = pffft_new_setup(N, PFFFT_REAL);
  float *x = floats(N), *spectrum = floats(N), *expected = floats(N);
  float *internal = floats(N), *reordered = floats(N), *work = floats(N);
  float *back = floats(N), *scaled = floats(N);
  int n;

  CHECK(setup != NULL);
  for (n = 0; n < N; ++n) {
    x[n] = (float)(1 + 2 * cos(2 * kPi * 5 * n / N) +
                   sin(2 * kPi * 3 * n / N) + 0.5 * (n % 2 ? -1 : 1) +
                   0.75 * sin(2 * kPi * 13 * n / N) +
                   1.5 * cos(2 * kPi * 20 * n / N) -
                   sin(2 * kPi * 27 * n / N));
  }
  expected[0] = N;
  expected[1] = N / 2;
  expected[2 * 5] = N;
  expected[2 * 3 + 1] = -N / 2;
  expected[2 * 13 + 1] = -3 * N / 8;
  expected[2 * 20] = 3 * N / 4;
  expected[2 * 27 + 1] = N / 2;
  pffft_transform_ordered(setup, x, spectrum, NULL, PFFFT_FORWARD);
  CHECK(near_all(spectrum, expected, N, 1e-3f * N));

  /* The ordered transform is the internal one followed by pffft_zreorder
     (pffft.c:1663-1664), so the two agree bit for bit; a caller's work
     area in place of the stack changes nothing. */
  pffft_transform(setup, x, internal, work, PFFFT_FORWARD);
  pffft_zreorder(setup, internal, reordered, PFFFT_FORWARD);
  CHECK(memcmp(reordered, spectrum, N * sizeof(float)) == 0);

  /* Unscaled: backward(forward(x)) = N x (pffft.h:117). */
  pffft_transform_ordered(setup, spectrum, back, NULL, PFFFT_BACKWARD);
  for (n = 0; n < N; ++n) {
    scaled[n] = x[n] * N;
  }
  CHECK(near_all(back, scaled, N, 1e-3f * N));

  pffft_destroy_setup(setup);
  pffft_aligned_free(x);
  pffft_aligned_free(spectrum);
  pffft_aligned_free(expected);
  pffft_aligned_free(internal);
  pffft_aligned_free(reordered);
  pffft_aligned_free(work);
  pffft_aligned_free(back);
  pffft_aligned_free(scaled);
}

static void check_complex(void) {
  /* N = 48 = 16 * 3 takes the radix-3 pass.  x[n] = e^(2 pi i 7n/N) +
     0.5 e^(-2 pi i 5n/N) has X[7] = N and X[N-5] = N/2. */
  enum { N = 48 };
  PFFFT_Setup *setup = pffft_new_setup(N, PFFFT_COMPLEX);
  float *x = floats(2 * N), *spectrum = floats(2 * N);
  float *expected = floats(2 * N), *back = floats(2 * N);
  float *scaled = floats(2 * N);
  int n;

  CHECK(setup != NULL);
  for (n = 0; n < N; ++n) {
    double a = 2 * kPi * 7 * n / N, b = -2 * kPi * 5 * n / N;
    x[2 * n] = (float)(cos(a) + 0.5 * cos(b));
    x[2 * n + 1] = (float)(sin(a) + 0.5 * sin(b));
  }
  expected[2 * 7] = N;
  expected[2 * (N - 5)] = N / 2;
  pffft_transform_ordered(setup, x, spectrum, NULL, PFFFT_FORWARD);
  CHECK(near_all(spectrum, expected, 2 * N, 1e-3f * N));

  pffft_transform_ordered(setup, spectrum, back, NULL, PFFFT_BACKWARD);
  for (n = 0; n < 2 * N; ++n) {
    scaled[n] = x[n] * N;
  }
  CHECK(near_all(back, scaled, 2 * N, 1e-3f * N));

  pffft_destroy_setup(setup);
  pffft_aligned_free(x);
  pffft_aligned_free(spectrum);
  pffft_aligned_free(expected);
  pffft_aligned_free(back);
  pffft_aligned_free(scaled);
}

static void check_convolution(void) {
  /* Circular convolution through the internal-order spectra, accumulated
     in two halves (pffft.h:152-164), against the direct sum. */
  enum { N = 96 };
  PFFFT_Setup *setup = pffft_new_setup(N, PFFFT_REAL);
  float *a = floats(N), *b = floats(N), *fa = floats(N), *fb = floats(N);
  float *fab = floats(N), *conv = floats(N), *direct = floats(N);
  int n, m;

  CHECK(setup != NULL);
  for (n = 0; n < N; ++n) {
    a[n] = (float)((n * 7) % 11 - 5);
  }
  b[0] = 1;
  b[1] = 2;
  b[2] = -1;
  b[N - 1] = 3;
  for (n = 0; n < N; ++n) {
    for (m = 0; m < N; ++m) {
      direct[n] += a[m] * b[(n - m + N) % N];
    }
  }
  pffft_transform(setup, a, fa, NULL, PFFFT_FORWARD);
  pffft_transform(setup, b, fb, NULL, PFFFT_FORWARD);
  pffft_zconvolve_accumulate(setup, fa, fb, fab, 0.5f / N);
  pffft_zconvolve_accumulate(setup, fa, fb, fab, 0.5f / N);
  pffft_transform(setup, fab, conv, NULL, PFFFT_BACKWARD);
  CHECK(near_all(conv, direct, N, 1e-3f * 16));

  pffft_destroy_setup(setup);
  pffft_aligned_free(a);
  pffft_aligned_free(b);
  pffft_aligned_free(fa);
  pffft_aligned_free(fb);
  pffft_aligned_free(fab);
  pffft_aligned_free(conv);
  pffft_aligned_free(direct);
}

static void check_setup_rules(void) {
  void *p = pffft_aligned_malloc(100);

  /* Lengths must factor into 2, 3 and 5 (pffft.h:62-65, :104-105). */
  CHECK(pffft_new_setup(32 * 7, PFFFT_REAL) == NULL);
  CHECK(pffft_new_setup(-32, PFFFT_REAL) == NULL);
  /* SIMD-compatible alignment: 16 bytes on x86 (pffft.h:67-69). */
  CHECK(p != NULL && (uintptr_t)p % 16 == 0);
  pffft_aligned_free(p);
#if defined(__x86_64__) || defined(_M_X64) || defined(__aarch64__) || \
    defined(_M_ARM64)
  /* SSE or NEON is chosen on these targets (pffft.c:99-112). */
  CHECK(pffft_simd_size() == 4);
#endif
}

int main(void) {
  check_real();
  check_complex();
  check_convolution();
  check_setup_rules();
  if (failures != 0) {
    return 1;
  }
  printf("pffft (SIMD width %d): C API OK\n", pffft_simd_size());
  return 0;
}
