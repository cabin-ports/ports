// Checks minimp3 configured for float output from C++: the streaming decoder
// of minimp3_ex.h with sample-accurate seeking over silent MPEG-1 Layer III
// frames, and mp3dec_f32_to_s16's scaling and clipping.
#define MINIMP3_FLOAT_OUTPUT
#define MINIMP3_IMPLEMENTATION
#include "minimp3_ex.h"

#include <cstdio>
#include <cstring>
#include <vector>

namespace {

int failures = 0;

void expect(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "failed: %s\n", what);
    ++failures;
  }
}

}  // namespace

int main() {
  // Four 417-byte frames of 1152 mono samples: MPEG-1 Layer III without CRC,
  // 128 kbit/s, 44.1 kHz, whose all-zero side information and main data code
  // silence.
  const int frame_bytes = 417, frames = 4, frame_samples = 1152;
  const unsigned char header[4] = {0xFF, 0xFB, 0x90, 0xC0};
  std::vector<uint8_t> stream(frame_bytes * frames);
  for (int i = 0; i < frames; ++i)
    std::memcpy(&stream[i * frame_bytes], header, sizeof header);

  mp3dec_ex_t dec;
  expect(mp3dec_ex_open_buf(&dec, stream.data(), stream.size(),
                            MP3D_SEEK_TO_SAMPLE) == 0,
         "open");
  expect(dec.samples == static_cast<uint64_t>(frame_samples) * frames &&
             dec.info.hz == 44100 && dec.info.channels == 1,
         "stream length");
  expect(mp3dec_ex_seek(&dec, 2000) == 0, "seek");
  std::vector<float> pcm(frame_samples * frames, 1.0f);
  const size_t read = mp3dec_ex_read(&dec, pcm.data(), pcm.size());
  bool silent = true;
  for (size_t i = 0; i < read; ++i) silent = silent && pcm[i] == 0.0f;
  expect(read == static_cast<size_t>(frame_samples) * frames - 2000 && silent,
         "read after seek");
  mp3dec_ex_close(&dec);

  // Values every rounding path (SSE, NEON, scalar) converts alike: the first
  // eight take the SIMD path where there is one, the last two the scalar loop.
  const float in[10] = {0.0f, 0.5f, -0.5f, 0.25f, -0.25f,
                        2.0f, -2.0f, 1.0f,  0.5f,  -1.0f};
  const int16_t want[10] = {0,      16384,  -16384, 8192,  -8192,
                            32767,  -32768, 32767,  16384, -32768};
  int16_t out[10];
  mp3dec_f32_to_s16(in, out, 10);
  expect(std::memcmp(out, want, sizeof want) == 0, "float to int16");

  if (failures != 0) return 1;
  std::puts("minimp3 C++ OK");
  return 0;
}
