#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ALACBitUtilities.h"
#include "ALACDecoder.h"
#include "ALACEncoder.h"

static int failures = 0;

#define CHECK(cond)                                                       \
  do {                                                                    \
    if (!(cond)) {                                                        \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);              \
      ++failures;                                                         \
    }                                                                     \
  } while (0)

static uint32_t be32(const uint8_t* p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}

// Encodes `frames` frames of `channels`-channel 16-bit PCM and decodes them
// back; the codec is lossless, so the samples must match exactly.
static void RoundTrip(uint32_t channels, uint32_t frames) {
  static int16_t in[4096 * 2];
  static int16_t out[4096 * 2];
  static uint8_t packet[4096 * 8 * 5 + 1];
  for (uint32_t i = 0; i < frames * channels; ++i) {
    in[i] = int16_t(int((i * 37u) % 2000u) - 1000 + int((i * i) % 7u) * (i % 2 ? -1 : 1));
  }
  in[0] = 32767;
  in[frames * channels - 1] = -32768;
  memset(out, 0x55, sizeof out);

  AudioFormatDescription pcm = {44100.0, kALACFormatLinearPCM,
                                kALACFormatFlagIsSignedInteger | kALACFormatFlagIsPacked,
                                2 * channels, 1, 2 * channels, channels, 16, 0};
  AudioFormatDescription alac = {44100.0, kALACFormatAppleLossless, 1, 0, 4096, 0, channels, 0, 0};

  ALACEncoder enc;
  enc.SetFrameSize(4096);
  CHECK(enc.InitializeEncoder(alac) == 0);

  // The cookie is the big-endian ALACSpecificConfig.
  uint8_t cookie[64];
  uint32_t cookieSize = sizeof cookie;
  CHECK(enc.GetMagicCookieSize(channels) == 24);
  enc.GetMagicCookie(cookie, &cookieSize);
  CHECK(cookieSize == 24);
  CHECK(be32(cookie) == 4096);                       // frameLength
  CHECK(cookie[4] == 0 && cookie[5] == 16);          // compatibleVersion, bitDepth
  CHECK(cookie[6] == 40 && cookie[7] == 10 && cookie[8] == 14);  // pb, mb, kb
  CHECK(cookie[9] == channels);
  CHECK(cookie[10] == 0 && cookie[11] == 255);       // maxRun
  CHECK(be32(cookie + 20) == 44100);                 // sampleRate

  int32_t bytes = int32_t(frames * 2 * channels);
  CHECK(enc.Encode(pcm, alac, reinterpret_cast<unsigned char*>(in), packet, &bytes) == 0);
  CHECK(bytes > 0 && bytes < int32_t(frames * 2 * channels));  // the ramp compresses
  CHECK((packet[0] >> 5) == (channels == 2 ? ID_CPE : ID_SCE));

  ALACDecoder dec;
  CHECK(dec.Init(cookie, cookieSize) == 0);
  CHECK(dec.mConfig.frameLength == 4096);
  CHECK(dec.mConfig.numChannels == channels);
  CHECK(dec.mConfig.sampleRate == 44100);

  BitBuffer bits;
  BitBufferInit(&bits, packet, uint32_t(bytes));
  uint32_t decoded = 0;
  CHECK(dec.Decode(&bits, reinterpret_cast<uint8_t*>(out), frames, channels, &decoded) == 0);
  CHECK(decoded == frames);
  CHECK(memcmp(in, out, frames * channels * sizeof(int16_t)) == 0);
}

int main() {
  RoundTrip(2, 4096);
  RoundTrip(1, 4096);
  RoundTrip(2, 1000);  // a short final packet
  if (failures) return 1;
  puts("alac ok");
  return 0;
}
