// Writes 32-bit float samples to a Wave64 file in memory with dr_wav and
// reads them back as 16-bit (scaled and clipped), then decodes silent
// MPEG-1 Layer III frames with dr_mp3's whole-stream helper.
#define DR_WAV_IMPLEMENTATION
#define DR_MP3_IMPLEMENTATION
#include "dr_mp3.h"
#include "dr_wav.h"

#include <cstdio>
#include <cstring>
#include <vector>

namespace {

int failures = 0;

void expect(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

void TestWave64() {
  const float samples[6] = {0.0f, 0.5f, -0.25f, 1.0f, -1.0f, 2.0f};
  const drwav_int16 expected[6] = {0, 16384, -8192, 32767, -32768, 32767};
  drwav_data_format format;
  format.container = drwav_container_w64;
  format.format = DR_WAVE_FORMAT_IEEE_FLOAT;
  format.channels = 1;
  format.sampleRate = 48000;
  format.bitsPerSample = 32;

  drwav wav;
  void* data = NULL;
  size_t size = 0;
  if (!drwav_init_memory_write(&wav, &data, &size, &format, NULL)) {
    expect(false, "w64 write init");
    return;
  }
  expect(drwav_write_pcm_frames(&wav, 6, samples) == 6, "w64 write");
  drwav_uninit(&wav);
  // Wave64 chunk IDs are GUIDs; the first begins with "riff".
  expect(size > 24 && std::memcmp(data, "riff", 4) == 0, "w64 header");

  std::vector<drwav_int16> s16(6);
  if (!drwav_init_memory(&wav, data, size, NULL)) {
    expect(false, "w64 read init");
    drwav_free(data, NULL);
    return;
  }
  expect(wav.container == drwav_container_w64 &&
             wav.translatedFormatTag == DR_WAVE_FORMAT_IEEE_FLOAT &&
             wav.sampleRate == 48000 && wav.totalPCMFrameCount == 6,
         "w64 format");
  const bool got = drwav_read_pcm_frames_s16(&wav, 6, &s16[0]) == 6;
  bool same = got;
  for (int i = 0; got && i < 6; ++i) same = same && s16[i] == expected[i];
  expect(same, "f32 to s16 scaling and clipping");
  drwav_uninit(&wav);
  drwav_free(data, NULL);
}

void TestMp3() {
  // Four 417-byte frames of 1152 mono samples: MPEG-1 Layer III without
  // CRC, 128 kbit/s, 44.1 kHz, whose all-zero side information and main
  // data code silence.
  const size_t frame_bytes = 417, frames = 4, frame_samples = 1152;
  const unsigned char header[4] = {0xFF, 0xFB, 0x90, 0xC0};
  std::vector<unsigned char> stream(frame_bytes * frames);
  for (size_t i = 0; i < frames; ++i)
    std::memcpy(&stream[i * frame_bytes], header, sizeof header);

  drmp3_config config = {0, 0};
  drmp3_uint64 count = 0;
  drmp3_int16* pcm = drmp3_open_memory_and_read_pcm_frames_s16(
      &stream[0], stream.size(), &config, &count, NULL);
  expect(pcm != NULL && config.channels == 1 && config.sampleRate == 44100 &&
             count == frame_samples * frames,
         "mp3 whole stream");
  if (pcm != NULL) {
    bool silent = true;
    for (drmp3_uint64 i = 0; i < count; ++i) silent = silent && pcm[i] == 0;
    expect(silent, "mp3 silence");
    drmp3_free(pcm, NULL);
  }
}

}  // namespace

int main() {
  TestWave64();
  TestMp3();
  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("dr_libs C++ consumer test passed");
  return 0;
}
