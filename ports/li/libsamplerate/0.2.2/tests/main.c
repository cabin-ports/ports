/* Consumer test from C: names, descriptions, error strings and ratio limits
   of every converter, the sample format conversions, exact zero-order-hold
   and linear output, and the sinc converters on a constant and on a sine. */
#include <samplerate.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures = 0;

static void check(int ok, const char* what, int line) {
  if (!ok) {
    fprintf(stderr, "main.c:%d: check failed: %s\n", line, what);
    ++failures;
  }
}
#define MAXD(a, b) ((a) > (b) ? (a) : (b))
#define CHECK(cond) check((cond) ? 1 : 0, #cond, __LINE__)

/* One src_simple call over `n` frames of `in`; returns the frames written. */
static long simple(const float* in, long n, float* out, long cap, double ratio,
                   int converter, long* used) {
  SRC_DATA d;
  memset(&d, 0, sizeof d);
  d.data_in = in;
  d.input_frames = n;
  d.data_out = out;
  d.output_frames = cap;
  d.src_ratio = ratio;
  CHECK(src_simple(&d, converter, 1) == 0);
  if (used) *used = d.input_frames_used;
  return d.output_frames_gen;
}

int main(void) {
  static const char* const names[] = {"Best Sinc Interpolator",
                                      "Medium Sinc Interpolator",
                                      "Fastest Sinc Interpolator",
                                      "ZOH Interpolator", "Linear Interpolator"};
  static const char* const descriptions[] = {
      "Band limited sinc interpolation, best quality, 144dB SNR, 96% BW.",
      "Band limited sinc interpolation, medium quality, 121dB SNR, 90% BW.",
      "Band limited sinc interpolation, fastest, 97dB SNR, 80% BW.",
      "Zero order hold interpolator, very fast, poor quality.",
      "Linear interpolator, very fast, poor quality."};
  int i, err = -1;

  CHECK(strcmp(src_get_version(),
               "libsamplerate-0.2.2 (c) 2002-2008 Erik de Castro Lopo") == 0);

  /* Every converter is built: the sinc ones depend on the ENABLE_SINC_*
     defines. */
  for (i = 0; i < 5; ++i) {
    SRC_STATE* s;
    CHECK(strcmp(src_get_name(i), names[i]) == 0);
    CHECK(strcmp(src_get_description(i), descriptions[i]) == 0);
    s = src_new(i, 2, &err);
    CHECK(s != NULL && err == 0);
    CHECK(s != NULL && src_get_channels(s) == 2 && src_error(s) == 0);
    CHECK(src_delete(s) == NULL);
  }
  CHECK(src_get_name(5) == NULL && src_get_description(5) == NULL);
  CHECK(src_new(5, 1, &err) == NULL && err == 10);
  CHECK(strcmp(src_strerror(err), "Bad converter number.") == 0);
  CHECK(strcmp(src_strerror(11), "Channel count must be >= 1.") == 0);
  CHECK(strcmp(src_strerror(0), "No error.") == 0);
  CHECK(strcmp(src_strerror(6), "SRC ratio outside [1/256, 256] range.") == 0);
  CHECK(strcmp(src_strerror(23),
               "Placeholder. No error defined for this error number.") == 0);
  CHECK(src_strerror(1000) == NULL);

  CHECK(src_is_valid_ratio(1.0) == 1 && src_is_valid_ratio(256.0) == 1);
  CHECK(src_is_valid_ratio(1.0 / 256.0) == 1);
  CHECK(src_is_valid_ratio(257.0) == 0 && src_is_valid_ratio(1.0 / 257.0) == 0);
  CHECK(src_is_valid_ratio(0.0) == 0);

  {
    static const short in[] = {0, 16384, -16384, 32767, -32768};
    static const float exp[] = {0.0f, 0.5f, -0.5f, 32767.0f / 32768.0f, -1.0f};
    float f[5];
    short back[5];
    src_short_to_float_array(in, f, 5);
    for (i = 0; i < 5; ++i) CHECK(f[i] == exp[i]);
    src_float_to_short_array(f, back, 5);
    for (i = 0; i < 5; ++i) CHECK(back[i] == in[i]);
  }
  {
    static const float in[] = {0.5f, 1.5f, -2.0f, 0.0f, 1.0f, -1.0f};
    static const short exp[] = {16384, 32767, -32768, 0, 32767, -32768};
    static const int exp_int[] = {0x40000000, 0x7fffffff, -0x7fffffff - 1,
                                  0,          0x7fffffff, -0x7fffffff - 1};
    short s[6];
    int n[6];
    float back[6];
    src_float_to_short_array(in, s, 6);
    for (i = 0; i < 6; ++i) CHECK(s[i] == exp[i]);
    src_float_to_int_array(in, n, 6);
    for (i = 0; i < 6; ++i) CHECK(n[i] == exp_int[i]);
    src_int_to_float_array(n, back, 6);
    CHECK(back[0] == 0.5f && back[3] == 0.0f && back[5] == -1.0f);
  }

  {
    static const float in[4] = {1.0f, 2.0f, 3.0f, 4.0f};
    static const float zoh2[9] = {1, 1, 1, 1, 2, 2, 3, 3, 4};
    static const float lin2[8] = {1.0f, 1.0f, 1.0f, 1.5f, 2.0f, 2.5f, 3.0f, 3.5f};
    float out[16];
    long used = 0, n;

    n = simple(in, 4, out, 16, 2.0, SRC_ZERO_ORDER_HOLD, &used);
    CHECK(n == 9 && used == 4);
    for (i = 0; i < 9; ++i) CHECK(out[i] == zoh2[i]);

    n = simple(in, 4, out, 16, 2.0, SRC_LINEAR, &used);
    CHECK(n == 8 && used == 4);
    for (i = 0; i < 8; ++i) CHECK(out[i] == lin2[i]);

    /* At ratio 1 both converters delay the signal by one frame. */
    n = simple(in, 4, out, 16, 1.0, SRC_ZERO_ORDER_HOLD, NULL);
    CHECK(n == 5);
    CHECK(out[0] == 1 && out[1] == 1 && out[2] == 2 && out[3] == 3 && out[4] == 4);
    n = simple(in, 4, out, 16, 1.0, SRC_LINEAR, NULL);
    CHECK(n == 4);
    CHECK(out[0] == 1 && out[1] == 1 && out[2] == 2 && out[3] == 3);
  }

  {
    /* Constant 0.5 and a sine, every sinc converter, upsampled by 2 then the
       output downsampled by 2 again. */
    enum { N = 1024 };
    static float dc[N], sine[N], up[2 * N + 64], down[N + 64];
    static const int sinc[3] = {SRC_SINC_BEST_QUALITY, SRC_SINC_MEDIUM_QUALITY,
                                SRC_SINC_FASTEST};
    static const long counts[3] = {2048, 2048, 2048};
    int c, k;
    for (k = 0; k < N; ++k) {
      dc[k] = 0.5f;
      sine[k] = (float)sin(2.0 * 3.14159265358979323846 * k / 64.0) * 0.5f;
    }
    for (c = 0; c < 3; ++c) {
      long used = 0, n;
      double worst = 0.0;
      n = simple(dc, N, up, 2 * N + 64, 2.0, sinc[c], &used);
      CHECK(n == counts[c] && used == N);
      for (k = 100; k < n - 100; ++k) worst = MAXD(worst, fabs(up[k] - 0.5));
      CHECK(worst < 1e-3);

      n = simple(sine, N, up, 2 * N + 64, 2.0, sinc[c], &used);
      CHECK(n == counts[c]);
      worst = 0.0;
      for (k = 100; k < n - 100; ++k)
        worst = MAXD(worst, fabs(up[k] - 0.5 * sin(2.0 * 3.14159265358979323846 * k / 128.0)));
      CHECK(worst < 2e-3);

      /* Halving the rate of the upsampled sine restores the original. */
      n = simple(up, n, down, N + 64, 0.5, sinc[c], &used);
      worst = 0.0;
      for (k = 100; k < n - 100 && k < N; ++k)
        worst = MAXD(worst, fabs(down[k] - sine[k]));
      CHECK(n == N && worst < 5e-3);
    }
  }

  if (failures) return 1;
  puts("libsamplerate OK");
  return 0;
}
