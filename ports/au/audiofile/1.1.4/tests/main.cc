#include <AudioFile.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what.c_str());
    ++failures;
  }
}

constexpr int kSamples = 512;

// Two opposite ramps over [-1, 1] in steps of 1/256, exact in float.
float Ramp(int channel, int n) {
  const int k = channel == 0 ? n - 256 : 256 - n;
  return static_cast<float>(k) / 256.0f;
}

bool HasBytes(const std::vector<uint8_t>& bytes, std::size_t at,
              const std::vector<uint8_t>& expected) {
  return bytes.size() >= at + expected.size() &&
         std::equal(expected.begin(), expected.end(), bytes.begin() + at);
}

// Saves a ramp at `bit_depth` in `format`, checks the encoded header and
// first sample, loads it back into AudioFile<double>, and checks the header
// and every sample against the ramp.
AudioFile<double> RoundTrip(int bit_depth, AudioFileFormat format,
                            const std::string& label) {
  AudioFile<float> source;
  source.setAudioBufferSize(2, kSamples);
  source.setSampleRate(44100);
  source.setBitDepth(bit_depth);
  for (int c = 0; c < 2; ++c) {
    for (int n = 0; n < kSamples; ++n) source.samples[c][n] = Ramp(c, n);
  }

  std::vector<uint8_t> bytes;
  check(source.saveToMemory(bytes, format), label + ": saveToMemory");
  // Channel 0 starts at -1.0, which encodes as -(2^(b-1) - 1): 0x80 0x00..
  // 0x01 big-endian.
  std::vector<uint8_t> first(bit_depth / 8, 0x00);
  first.front() = 0x80;
  first.back() = 0x01;
  if (format == AudioFileFormat::Wave) {
    check(HasBytes(bytes, 0, {'R', 'I', 'F', 'F'}) &&
              HasBytes(bytes, 8, {'W', 'A', 'V', 'E'}),
          label + ": RIFF/WAVE header");
    check(HasBytes(bytes, 24, {0x44, 0xAC, 0x00, 0x00}),
          label + ": little-endian sample rate");
    std::reverse(first.begin(), first.end());
    check(HasBytes(bytes, 44, first), label + ": little-endian first sample");
  } else {
    check(HasBytes(bytes, 0, {'F', 'O', 'R', 'M'}) &&
              HasBytes(bytes, 8, {'A', 'I', 'F', 'F'}),
          label + ": FORM/AIFF header");
    check(HasBytes(bytes, 28, {0x40, 0x0E, 0xAC, 0x44, 0, 0, 0, 0, 0, 0}),
          label + ": 80-bit extended sample rate");
    check(HasBytes(bytes, 54, first), label + ": big-endian first sample");
  }

  AudioFile<double> loaded;
  check(loaded.loadFromMemory(bytes), label + ": loadFromMemory");
  check(loaded.getSampleRate() == 44100, label + ": sample rate");
  check(loaded.getNumChannels() == 2 && loaded.isStereo(),
        label + ": channels");
  check(loaded.getNumSamplesPerChannel() == kSamples, label + ": samples");
  check(loaded.getBitDepth() == bit_depth, label + ": bit depth");
  if (loaded.getNumChannels() != 2 ||
      loaded.getNumSamplesPerChannel() != kSamples) {
    return loaded;
  }
  // A sample is truncated to n / (2^(b-1) - 1), so it lies within one step.
  const double step = 1.0 / ((1 << (bit_depth - 1)) - 1);
  bool within = true;
  for (int c = 0; c < 2; ++c) {
    for (int n = 0; n < kSamples; ++n) {
      within = within && std::fabs(loaded.samples[c][n] - Ramp(c, n)) < step;
    }
  }
  check(within, label + ": samples within one quantisation step");
  // Full scale and silence are exact at every depth.
  check(loaded.samples[0][0] == -1.0 && loaded.samples[1][0] == 1.0 &&
            loaded.samples[0][256] == 0.0 && loaded.samples[1][256] == 0.0,
        label + ": exact -1, 0 and 1");
  return loaded;
}

}  // namespace

int main() {
  AudioFile<double> wav16 = RoundTrip(16, AudioFileFormat::Wave, "WAV 16-bit");
  AudioFile<double> aiff16 =
      RoundTrip(16, AudioFileFormat::Aiff, "AIFF 16-bit");
  check(wav16.samples == aiff16.samples, "WAV and AIFF decode alike");
  RoundTrip(24, AudioFileFormat::Wave, "WAV 24-bit");
  RoundTrip(24, AudioFileFormat::Aiff, "AIFF 24-bit");

  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("audiofile consumer test passed");
  return 0;
}
