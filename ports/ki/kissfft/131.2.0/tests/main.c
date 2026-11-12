/* Consumer: every C API, complex and real, one- and multi-dimensional,
   forward and inverse, against a direct DFT computed in double precision,
   over sizes that reach each butterfly (radix 2, 3, 4, 5 and generic). */
#include <kiss_fft.h>
#include <kissfft/kiss_fftndr.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static int failures = 0;

#define CHECK(cond)                                             \
  do {                                                          \
    if (!(cond)) {                                              \
      printf("main.c:%d: check failed: %s\n", __LINE__, #cond); \
      ++failures;                                               \
    }                                                           \
  } while (0)

static const double two_pi = 6.283185307179586476925286766559;

/* Direct DFT, the sign giving the direction (kissfft's forward transform
   uses exp(-2 pi i jk/n), its inverse exp(+2 pi i jk/n), neither scaled). */
static void dft(const double *re, const double *im, int n, int sign, double *out_re, double *out_im) {
  int k, j;
  for (k = 0; k < n; ++k) {
    double sr = 0, si = 0;
    for (j = 0; j < n; ++j) {
      const double a = sign * two_pi * (double)((long)j * k % n) / n;
      sr += re[j] * cos(a) - im[j] * sin(a);
      si += re[j] * sin(a) + im[j] * cos(a);
    }
    out_re[k] = sr;
    out_im[k] = si;
  }
}

/* The larger of two errors; a NaN wins and stays, so its check fails. */
static double worse(double err, double d) { return (err != err || err >= d) ? err : d; }

/* Error bound for float results of a transform whose outputs are sums of n
   inputs below 1.25 in magnitude; an inverse divided by n is the input to
   within inverse_tolerance. */
static double tolerance(int n) { return 2e-5 * n; }
static const double inverse_tolerance = 3e-5;

static double input(int i, int part) { return sin(0.37 * i + 1.3 * part) + 0.25 * cos(1.9 * i); }

static void check_complex(int n) {
  const int before = failures;
  kiss_fft_cpx *in = malloc(sizeof *in * 2 * n), *out = malloc(sizeof *out * n),
               *back = malloc(sizeof *back * n), *strided = malloc(sizeof *strided * n);
  double *re = malloc(sizeof *re * n), *im = malloc(sizeof *im * n), *ref_re = malloc(sizeof *ref_re * n),
         *ref_im = malloc(sizeof *ref_im * n);
  kiss_fft_cfg fwd = kiss_fft_alloc(n, 0, NULL, NULL), inv = kiss_fft_alloc(n, 1, NULL, NULL);
  double err = 0;
  int i;
  CHECK(fwd != NULL && inv != NULL);
  for (i = 0; i < n; ++i) {
    in[i].r = (float)input(i, 0);
    in[i].i = (float)input(i, 1);
    re[i] = in[i].r;
    im[i] = in[i].i;
  }
  dft(re, im, n, -1, ref_re, ref_im);
  kiss_fft(fwd, in, out);
  for (i = 0; i < n; ++i) err = worse(err, worse(fabs(out[i].r - ref_re[i]), fabs(out[i].i - ref_im[i])));
  CHECK(err <= tolerance(n));

  /* The inverse is unscaled: it returns n times the input. */
  kiss_fft(inv, out, back);
  err = 0;
  for (i = 0; i < n; ++i) err = worse(err, worse(fabs(back[i].r / n - in[i].r), fabs(back[i].i / n - in[i].i)));
  CHECK(err <= inverse_tolerance);

  /* In place, through kissfft's temporary copy. */
  for (i = 0; i < n; ++i) back[i] = in[i];
  kiss_fft(fwd, back, back);
  err = 0;
  for (i = 0; i < n; ++i) err = worse(err, worse(fabs(back[i].r - out[i].r), fabs(back[i].i - out[i].i)));
  CHECK(err == 0);

  /* Every second input sample. */
  for (i = 0; i < 2 * n; ++i) {
    in[i].r = (float)input(i, 2);
    in[i].i = (float)input(i, 3);
  }
  for (i = 0; i < n; ++i) {
    re[i] = in[2 * i].r;
    im[i] = in[2 * i].i;
  }
  dft(re, im, n, -1, ref_re, ref_im);
  kiss_fft_stride(fwd, in, strided, 2);
  err = 0;
  for (i = 0; i < n; ++i) err = worse(err, worse(fabs(strided[i].r - ref_re[i]), fabs(strided[i].i - ref_im[i])));
  CHECK(err <= tolerance(n));

  kiss_fft_free(fwd);
  kiss_fft_free(inv);
  free(in), free(out), free(back), free(strided), free(re), free(im), free(ref_re), free(ref_im);
  if (failures != before) printf("  complex n=%d\n", n);
}

static void check_real(int n) {
  const int before = failures, bins = n / 2 + 1;
  float *in = malloc(sizeof *in * n), *back = malloc(sizeof *back * n);
  kiss_fft_cpx *out = malloc(sizeof *out * bins);
  double *re = malloc(sizeof *re * n), *im = calloc(n, sizeof *im), *ref_re = malloc(sizeof *ref_re * n),
         *ref_im = malloc(sizeof *ref_im * n);
  kiss_fftr_cfg fwd = kiss_fftr_alloc(n, 0, NULL, NULL), inv = kiss_fftr_alloc(n, 1, NULL, NULL);
  double err = 0;
  int i;
  CHECK(fwd != NULL && inv != NULL);
  for (i = 0; i < n; ++i) re[i] = in[i] = (float)input(i, 4);
  dft(re, im, n, -1, ref_re, ref_im);
  kiss_fftr(fwd, in, out);
  /* The half spectrum, DC to Nyquist. */
  for (i = 0; i < bins; ++i) err = worse(err, worse(fabs(out[i].r - ref_re[i]), fabs(out[i].i - ref_im[i])));
  CHECK(err <= tolerance(n));
  kiss_fftri(inv, out, back);
  err = 0;
  for (i = 0; i < n; ++i) err = worse(err, fabs(back[i] / n - in[i]));
  CHECK(err <= inverse_tolerance);
  kiss_fftr_free(fwd);
  kiss_fftr_free(inv);
  free(in), free(back), free(out), free(re), free(im), free(ref_re), free(ref_im);
  if (failures != before) printf("  real n=%d\n", n);
}

/* A 2-D or 3-D transform is the 1-D one along each axis in turn. */
static void dft_nd(const int *dims, int ndims, double *re, double *im) {
  int total = 1, axis, d;
  for (d = 0; d < ndims; ++d) total *= dims[d];
  for (axis = 0; axis < ndims; ++axis) {
    int inner = 1, start;
    double row_re[64], row_im[64], out_re[64], out_im[64];
    for (d = axis + 1; d < ndims; ++d) inner *= dims[d];
    for (start = 0; start < total; ++start) {
      int k;
      if ((start / inner) % dims[axis] != 0) continue;
      for (k = 0; k < dims[axis]; ++k) {
        row_re[k] = re[start + k * inner];
        row_im[k] = im[start + k * inner];
      }
      dft(row_re, row_im, dims[axis], -1, out_re, out_im);
      for (k = 0; k < dims[axis]; ++k) {
        re[start + k * inner] = out_re[k];
        im[start + k * inner] = out_im[k];
      }
    }
  }
}

static void check_nd(const int *dims, int ndims) {
  const int before = failures, last = dims[ndims - 1], half = last / 2 + 1;
  int total = 1, d, i;
  kiss_fft_cpx in[120], out[120], back[120];
  float rin[120], rback[120];
  double re[120], im[120], err = 0;
  kiss_fftnd_cfg fwd = kiss_fftnd_alloc(dims, ndims, 0, NULL, NULL), inv = kiss_fftnd_alloc(dims, ndims, 1, NULL, NULL);
  kiss_fftndr_cfg rfwd = kiss_fftndr_alloc(dims, ndims, 0, NULL, NULL),
                  rinv = kiss_fftndr_alloc(dims, ndims, 1, NULL, NULL);
  CHECK(fwd != NULL && inv != NULL && rfwd != NULL && rinv != NULL);
  for (d = 0; d < ndims; ++d) total *= dims[d];
  for (i = 0; i < total; ++i) {
    re[i] = in[i].r = (float)input(i, 5);
    im[i] = in[i].i = (float)input(i, 6);
  }
  dft_nd(dims, ndims, re, im);
  kiss_fftnd(fwd, in, out);
  for (i = 0; i < total; ++i) err = worse(err, worse(fabs(out[i].r - re[i]), fabs(out[i].i - im[i])));
  CHECK(err <= tolerance(total));
  kiss_fftnd(inv, out, back);
  err = 0;
  for (i = 0; i < total; ++i) err = worse(err, worse(fabs(back[i].r / total - in[i].r), fabs(back[i].i / total - in[i].i)));
  CHECK(err <= 1e-4);

  /* Real input: the last axis keeps its half spectrum. */
  for (i = 0; i < total; ++i) {
    re[i] = rin[i] = (float)input(i, 7);
    im[i] = 0;
  }
  dft_nd(dims, ndims, re, im);
  kiss_fftndr(rfwd, rin, out);
  err = 0;
  for (i = 0; i < total / last * half; ++i) {
    const int j = i / half * last + i % half;
    err = worse(err, worse(fabs(out[i].r - re[j]), fabs(out[i].i - im[j])));
  }
  CHECK(err <= tolerance(total));
  kiss_fftndri(rinv, out, rback);
  err = 0;
  for (i = 0; i < total; ++i) err = worse(err, fabs(rback[i] / total - rin[i]));
  CHECK(err <= 1e-4);
  free(fwd), free(inv), kiss_fftndr_free(rfwd), kiss_fftndr_free(rinv);
  if (failures != before) printf("  nd dims[0]=%d ndims=%d\n", dims[0], ndims);
}

static int only_2_3_5(int n) {
  while (n % 2 == 0) n /= 2;
  while (n % 3 == 0) n /= 3;
  while (n % 5 == 0) n /= 5;
  return n == 1;
}

int main(void) {
  static const int sizes[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 12, 15, 16, 25, 30, 32, 49, 60, 64, 77, 97, 120, 128, 243, 256, 1000};
  static const int dims2[] = {6, 4}, dims3[] = {3, 5, 8};
  unsigned i;
  int n;
  for (i = 0; i < sizeof sizes / sizeof sizes[0]; ++i) {
    check_complex(sizes[i]);
    if (sizes[i] % 2 == 0) check_real(sizes[i]);
  }
  check_nd(dims2, 2);
  check_nd(dims3, 3);

  /* Known transforms: an impulse has a flat spectrum, and the inverse of a
     single bin is a complex exponential. */
  {
    kiss_fft_cpx x[12] = {{0, 0}}, y[12];
    kiss_fft_cfg cfg = kiss_fft_alloc(12, 0, NULL, NULL), icfg = kiss_fft_alloc(12, 1, NULL, NULL);
    double err = 0;
    x[0].r = 1;
    kiss_fft(cfg, x, y);
    for (n = 0; n < 12; ++n) err = worse(err, worse(fabs(y[n].r - 1), fabs(y[n].i)));
    CHECK(err <= 1e-6);
    x[0].r = 0;
    x[3].r = 1;
    kiss_fft(icfg, x, y);
    err = 0;
    for (n = 0; n < 12; ++n) err = worse(err, worse(fabs(y[n].r - cos(two_pi * 3 * n / 12)), fabs(y[n].i - sin(two_pi * 3 * n / 12))));
    CHECK(err <= 1e-6);
    free(cfg);
    free(icfg);
  }

  /* A caller-supplied buffer: a size query, too small, then large enough. */
  {
    size_t len = 0, small;
    char *mem;
    kiss_fft_cfg cfg;
    CHECK(kiss_fft_alloc(100, 0, NULL, &len) == NULL && len > 0);
    mem = malloc(len);
    small = len - 1;
    CHECK(kiss_fft_alloc(100, 0, mem, &small) == NULL);
    cfg = kiss_fft_alloc(100, 0, mem, &len);
    CHECK(cfg == (kiss_fft_cfg)mem);
    {
      kiss_fft_cpx x[100], y[100], z[100];
      kiss_fft_cfg heap = kiss_fft_alloc(100, 0, NULL, NULL);
      for (n = 0; n < 100; ++n) {
        x[n].r = (float)input(n, 8);
        x[n].i = (float)input(n, 9);
      }
      kiss_fft(cfg, x, y);
      kiss_fft(heap, x, z);
      for (n = 0; n < 100; ++n) CHECK(y[n].r == z[n].r && y[n].i == z[n].i);
      kiss_fft_free(heap);
    }
    free(mem);
    len = 0;
    CHECK(kiss_fftr_alloc(100, 0, NULL, &len) == NULL && len > 0);
  }

  /* The real transform needs an even size. */
  CHECK(kiss_fftr_alloc(7, 0, NULL, NULL) == NULL);

  /* The smallest size not below n with only the factors 2, 3 and 5, and
     the smallest even one for the real transform. */
  for (n = 1; n <= 2000; ++n) {
    int k = n, even = n + n % 2;
    while (!only_2_3_5(k)) ++k;
    while (!only_2_3_5(even)) even += 2;
    CHECK(kiss_fft_next_fast_size(n) == k);
    CHECK(kiss_fftr_next_fast_size_real(n) == even);
    if (failures > 20) break;
  }
  CHECK(kiss_fft_next_fast_size(97) == 100 && kiss_fft_next_fast_size(1001) == 1024);

  kiss_fft_cleanup();
  printf("kissfft: %s\n", failures == 0 ? "ok" : "FAILED");
  return failures == 0 ? 0 : 1;
}
