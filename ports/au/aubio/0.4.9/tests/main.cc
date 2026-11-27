// The explicit WAV source and sink are declared under AUBIO_UNSTABLE
// (aubio.h).
#define AUBIO_UNSTABLE 1
#include <aubio/aubio.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

int failures = 0;

#define CHECK(cond)                                                     \
  do {                                                                  \
    if (!(cond)) {                                                      \
      std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__,       \
                   __LINE__, #cond);                                    \
      ++failures;                                                       \
    }                                                                   \
  } while (0)

const double kTwoPi = 6.283185307179586;
const uint_t kRate = 44100;
const uint_t kHop = 256;
const uint_t kFrames = 4 * kHop;

// 16-bit PCM values, which a 16-bit WAV file stores exactly.
std::vector<smpl_t> pcm_ramp() {
  std::vector<smpl_t> samples(kFrames);
  for (uint_t i = 0; i < kFrames; ++i) {
    samples[i] = static_cast<smpl_t>(static_cast<int>((i * 37) % 4001) - 2000) / 32768;
  }
  return samples;
}

// Among the probe's own build outputs: `cabin run` keeps the caller's
// working directory.
std::string temp_path(const char* name) {
  const char* dir = std::getenv("CABIN_PROFILE_DIR");
  return dir == NULL ? std::string() : std::string(dir) + "/" + name;
}

// A sink writes the ramp in hops; a source reading the file gives it back.
template <typename Sink, typename Source>
void check_round_trip(const std::string& path, Sink* (*new_sink)(const char_t*, uint_t),
                      void (*sink_do)(Sink*, fvec_t*, uint_t), void (*del_sink)(Sink*),
                      Source* (*new_source)(const char_t*, uint_t, uint_t),
                      uint_t (*get_samplerate)(Source*),
                      void (*source_do)(Source*, fvec_t*, uint_t*),
                      void (*del_source)(Source*)) {
  const std::vector<smpl_t> samples = pcm_ramp();
  CHECK(!path.empty());
  if (path.empty()) return;
  fvec_t* hop = new_fvec(kHop);

  Sink* sink = new_sink(path.c_str(), kRate);
  CHECK(sink != NULL);
  if (sink == NULL) {
    del_fvec(hop);
    return;
  }
  for (uint_t start = 0; start < kFrames; start += kHop) {
    for (uint_t i = 0; i < kHop; ++i) fvec_set_sample(hop, samples[start + i], i);
    sink_do(sink, hop, kHop);
  }
  del_sink(sink);

  Source* source = new_source(path.c_str(), 0, kHop);
  CHECK(source != NULL);
  if (source == NULL) {
    del_fvec(hop);
    return;
  }
  CHECK(get_samplerate(source) == kRate);
  for (uint_t start = 0; start < kFrames; start += kHop) {
    uint_t read = 0;
    source_do(source, hop, &read);
    CHECK(read == kHop);
    for (uint_t i = 0; i < kHop; ++i) {
      CHECK(fvec_get_sample(hop, i) == samples[start + i]);
    }
  }
  uint_t read = 1;
  source_do(source, hop, &read);
  CHECK(read == 0);
  del_source(source);
  del_fvec(hop);
  std::remove(path.c_str());
}

// Gain in dB of the A-weighting filter on a steady sine, once settled.
double a_weighting_gain_db(double freq) {
  const uint_t n = 16384;
  aubio_filter_t* filter = new_aubio_filter_a_weighting(kRate);
  CHECK(filter != NULL);
  if (filter == NULL) return 0;
  fvec_t* in = new_fvec(n);
  fvec_t* out = new_fvec(n);
  for (uint_t i = 0; i < n; ++i) {
    fvec_set_sample(in, static_cast<smpl_t>(0.5 * std::sin(kTwoPi * freq * i / kRate)), i);
  }
  aubio_filter_do_outplace(filter, in, out);
  double in_energy = 0, out_energy = 0;
  for (uint_t i = n / 2; i < n; ++i) {
    in_energy += fvec_get_sample(in, i) * fvec_get_sample(in, i);
    out_energy += fvec_get_sample(out, i) * fvec_get_sample(out, i);
  }
  del_fvec(out);
  del_fvec(in);
  del_aubio_filter(filter);
  return 10 * std::log10(out_energy / in_energy);
}

}  // namespace

int main() {
  check_round_trip(temp_path("port-probe-aubio-wavwrite.wav"), new_aubio_sink_wavwrite,
                   aubio_sink_wavwrite_do, del_aubio_sink_wavwrite, new_aubio_source_wavread,
                   aubio_source_wavread_get_samplerate, aubio_source_wavread_do,
                   del_aubio_source_wavread);
  // The default backends: AudioToolbox on macOS, the WAV ones elsewhere.
  check_round_trip(temp_path("port-probe-aubio-sink.wav"), new_aubio_sink, aubio_sink_do,
                   del_aubio_sink, new_aubio_source, aubio_source_get_samplerate,
                   aubio_source_do, del_aubio_source);

  // IEC 61672-1 A-weighting: 0 dB at 1 kHz, -19.1 dB at 100 Hz.
  CHECK(std::fabs(a_weighting_gain_db(1000)) < 0.1);
  CHECK(std::fabs(a_weighting_gain_db(100) + 19.1) < 0.1);

  aubio_cleanup();
  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  return 0;
}
