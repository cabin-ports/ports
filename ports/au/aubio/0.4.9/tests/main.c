/* Individual installed headers, in aubio.h's order; main.cc uses aubio.h. */
#include <aubio/types.h>
#include <aubio/fvec.h>
#include <aubio/cvec.h>
#include <aubio/musicutils.h>
#include <aubio/spectral/fft.h>
#include <aubio/pitch/pitch.h>

#include <math.h>
#include <stdio.h>

static int failures = 0;

#define CHECK(cond)                                               \
  do {                                                            \
    if (!(cond)) {                                                \
      fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__,      \
              __LINE__, #cond);                                   \
      ++failures;                                                 \
    }                                                             \
  } while (0)

static const double two_pi = 6.283185307179586;

/* MIDI note 69 is A4, 440 Hz. */
static void check_music_conversions(void) {
  CHECK(fabsf(aubio_miditofreq(69.f) - 440.f) < 1e-3f);
  CHECK(fabsf(aubio_miditofreq(81.f) - 880.f) < 2e-3f);
  CHECK(fabsf(aubio_freqtomidi(440.f) - 69.f) < 1e-4f);
  CHECK(fabsf(aubio_freqtomidi(220.f) - 57.f) < 1e-4f);
}

/* A single impulse of height 0.5 at sample 10 has a flat magnitude spectrum
   of 0.5 and phase -2*pi*k*10/256 in bin k; the inverse transform gives the
   impulse back. */
static void check_fft(void) {
  const uint_t n = 256;
  aubio_fft_t *fft = new_aubio_fft(n);
  fvec_t *in = new_fvec(n);
  fvec_t *out = new_fvec(n);
  cvec_t *spectrum = new_cvec(n);
  uint_t i;
  CHECK(fft != NULL);
  if (fft == NULL) return;
  fvec_set_sample(in, 0.5f, 10);
  aubio_fft_do(fft, in, spectrum);
  CHECK(spectrum->length == n / 2 + 1);
  for (i = 0; i < spectrum->length; ++i) {
    CHECK(fabsf(cvec_norm_get_sample(spectrum, i) - 0.5f) < 1e-6f);
  }
  CHECK(fabs(cvec_phas_get_sample(spectrum, 1) + two_pi * 10 / n) < 1e-5);
  aubio_fft_rdo(fft, spectrum, out);
  for (i = 0; i < n; ++i) {
    CHECK(fabsf(fvec_get_sample(out, i) - fvec_get_sample(in, i)) < 1e-6f);
  }
  del_cvec(spectrum);
  del_fvec(out);
  del_fvec(in);
  del_aubio_fft(fft);
}

/* The pitch in Hz a method reports after 16 hops of a steady 440 Hz sine at
   44.1 kHz, and its confidence. */
static smpl_t detect_pitch(const char_t *method, smpl_t *confidence) {
  const uint_t buf_size = 2048, hop_size = 512, samplerate = 44100;
  aubio_pitch_t *pitch = new_aubio_pitch(method, buf_size, hop_size, samplerate);
  fvec_t *in, *out;
  uint_t hop, i, t = 0;
  smpl_t freq;
  CHECK(pitch != NULL);
  if (pitch == NULL) return 0;
  in = new_fvec(hop_size);
  out = new_fvec(1);
  for (hop = 0; hop < 16; ++hop) {
    for (i = 0; i < hop_size; ++i, ++t) {
      fvec_set_sample(in, (smpl_t)(0.5 * sin(two_pi * 440.0 * t / samplerate)), i);
    }
    aubio_pitch_do(pitch, in, out);
  }
  freq = fvec_get_sample(out, 0);
  *confidence = aubio_pitch_get_confidence(pitch);
  del_fvec(out);
  del_fvec(in);
  del_aubio_pitch(pitch);
  return freq;
}

int main(void) {
  smpl_t confidence = 0;
  check_music_conversions();
  check_fft();
  CHECK(fabsf(detect_pitch("yin", &confidence) - 440.f) < 0.1f);
  CHECK(confidence > 0.99f);
  /* "default" is yinfft. */
  CHECK(fabsf(detect_pitch("default", &confidence) - 440.f) < 1.f);
  aubio_cleanup();
  if (failures != 0) {
    fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  return 0;
}
