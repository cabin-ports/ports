/* Round-trips PCM through dr_wav in memory, decodes a hand-built FLAC stream
   (verbatim samples behind checked CRCs) with dr_flac, and decodes silent
   MPEG-1 Layer III frames with dr_mp3. */
#define DR_WAV_IMPLEMENTATION
#define DR_FLAC_IMPLEMENTATION
#define DR_MP3_IMPLEMENTATION
#include "dr_flac.h"
#include "dr_mp3.h"
#include "dr_wav.h"

#include <stdio.h>
#include <string.h>

static int failures;

static void expect(int ok, const char* what) {
  if (!ok) {
    fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

static void test_wav(void) {
  /* Four stereo frames. */
  static const drwav_int16 frames[8] = {0,      16384, -16384, 32767,
                                        -32768, 1,     -1,     1234};
  drwav_data_format format;
  drwav wav;
  void* data = NULL;
  size_t size = 0;
  drwav_int16 s16[8];
  float f32[8];
  int i, same = 1;

  format.container = drwav_container_riff;
  format.format = DR_WAVE_FORMAT_PCM;
  format.channels = 2;
  format.sampleRate = 22050;
  format.bitsPerSample = 16;
  if (!drwav_init_memory_write(&wav, &data, &size, &format, NULL)) {
    expect(0, "wav write init");
    return;
  }
  expect(drwav_write_pcm_frames(&wav, 4, frames) == 4, "wav write");
  drwav_uninit(&wav);
  /* A 44-byte header and 16 bytes of samples. */
  expect(size == 60 && memcmp(data, "RIFF", 4) == 0 &&
             memcmp((const char*)data + 8, "WAVE", 4) == 0,
         "wav header");

  if (!drwav_init_memory(&wav, data, size, NULL)) {
    expect(0, "wav read init");
    drwav_free(data, NULL);
    return;
  }
  expect(wav.channels == 2 && wav.sampleRate == 22050 &&
             wav.bitsPerSample == 16 && wav.totalPCMFrameCount == 4,
         "wav format");
  if (drwav_read_pcm_frames_s16(&wav, 4, s16) == 4) {
    for (i = 0; i < 8; ++i) same &= s16[i] == frames[i];
    expect(same, "wav samples");
  } else {
    expect(0, "wav read s16");
  }
  expect(drwav_seek_to_pcm_frame(&wav, 0) &&
             drwav_read_pcm_frames_f32(&wav, 4, f32) == 4 &&
             f32[1] == 0.5f && f32[2] == -0.5f && f32[4] == -1.0f,
         "wav f32 conversion");
  drwav_uninit(&wav);
  drwav_free(data, NULL);
}

static unsigned char crc8(const unsigned char* p, size_t n) {
  unsigned crc = 0;
  size_t i;
  int bit;
  for (i = 0; i < n; ++i) {
    crc ^= p[i];
    for (bit = 0; bit < 8; ++bit)
      crc = (crc & 0x80 ? (crc << 1) ^ 0x07 : crc << 1) & 0xFF;
  }
  return (unsigned char)crc;
}

static unsigned crc16(const unsigned char* p, size_t n) {
  unsigned crc = 0;
  size_t i;
  int bit;
  for (i = 0; i < n; ++i) {
    crc ^= (unsigned)p[i] << 8;
    for (bit = 0; bit < 8; ++bit)
      crc = (crc & 0x8000 ? (crc << 1) ^ 0x8005 : crc << 1) & 0xFFFF;
  }
  return crc;
}

static void test_flac(void) {
  static const drflac_int16 samples[4] = {1000, -2000, 3000, -32768};
  /* fLaC, then the last metadata block: STREAMINFO, 34 bytes. */
  static const unsigned char head[42] = {
      'f', 'L', 'a', 'C', 0x80, 0, 0, 34,
      0, 16, 0, 16,        /* block size 16; the last block may be shorter */
      0, 0, 0, 0, 0, 0,    /* frame sizes unknown */
      /* 44100 Hz (20 bits), mono (3), 16 bits (5), 4 samples (36) */
      0x0A, 0xC4, 0x40, 0xF0, 0, 0, 0, 4,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}; /* no MD5 */
  unsigned char stream[64];
  size_t n, frame;
  unsigned crc;
  unsigned int channels = 0, rate = 0;
  drflac_uint64 count = 0;
  drflac_int16* pcm;
  drflac* flac;
  drflac_int16 tail[2];
  int i, same = 1;

  memcpy(stream, head, sizeof head);
  n = frame = sizeof head;
  stream[n++] = 0xFF; /* sync, fixed block size */
  stream[n++] = 0xF8;
  stream[n++] = 0x69; /* 8-bit block size - 1 follows; 44.1 kHz */
  stream[n++] = 0x08; /* mono, 16 bits per sample */
  stream[n++] = 0x00; /* frame 0 */
  stream[n++] = 0x03;
  stream[n] = crc8(stream + frame, n - frame);
  ++n;
  stream[n++] = 0x02; /* VERBATIM subframe */
  for (i = 0; i < 4; ++i) {
    unsigned v = (unsigned short)samples[i];
    stream[n++] = (unsigned char)(v >> 8);
    stream[n++] = (unsigned char)(v & 0xFF);
  }
  crc = crc16(stream + frame, n - frame);
  stream[n++] = (unsigned char)(crc >> 8);
  stream[n++] = (unsigned char)(crc & 0xFF);

  pcm = drflac_open_memory_and_read_pcm_frames_s16(stream, n, &channels,
                                                   &rate, &count, NULL);
  expect(pcm != NULL && channels == 1 && rate == 44100 && count == 4,
         "flac format");
  if (pcm != NULL) {
    for (i = 0; i < 4; ++i) same &= pcm[i] == samples[i];
    expect(same, "flac samples");
    drflac_free(pcm, NULL);
  }

  flac = drflac_open_memory(stream, n, NULL);
  expect(flac != NULL, "flac open");
  if (flac != NULL) {
    expect(flac->bitsPerSample == 16 && flac->totalPCMFrameCount == 4 &&
               drflac_seek_to_pcm_frame(flac, 2) &&
               drflac_read_pcm_frames_s16(flac, 2, tail) == 2 &&
               tail[0] == 3000 && tail[1] == -32768,
           "flac seek");
    drflac_close(flac);
  }
}

static void test_mp3(void) {
  /* 1152 samples at 128 kbit/s and 44.1 kHz, unpadded: 417 bytes.  All-zero
     side information and main data code silence. */
  enum { FRAME_BYTES = 417, FRAMES = 4, FRAME_SAMPLES = 1152 };
  static const unsigned char header[4] = {0xFF, 0xFB, 0x90, 0xC0};
  static unsigned char stream[FRAME_BYTES * FRAMES];
  static float pcm[FRAME_SAMPLES * FRAMES];
  drmp3 mp3;
  int i, silent = 1;
  drmp3_uint64 got;

  for (i = 0; i < FRAMES; ++i)
    memcpy(stream + i * FRAME_BYTES, header, sizeof header);
  for (i = 0; i < FRAME_SAMPLES * FRAMES; ++i) pcm[i] = 1.0f;

  if (!drmp3_init_memory(&mp3, stream, sizeof stream, NULL)) {
    expect(0, "mp3 init");
    return;
  }
  expect(mp3.channels == 1 && mp3.sampleRate == 44100, "mp3 format");
  expect(drmp3_get_pcm_frame_count(&mp3) == FRAME_SAMPLES * FRAMES,
         "mp3 frame count");
  got = drmp3_seek_to_pcm_frame(&mp3, 1000)
             ? drmp3_read_pcm_frames_f32(&mp3, FRAME_SAMPLES * FRAMES, pcm)
             : 0;
  expect(got == FRAME_SAMPLES * FRAMES - 1000, "mp3 seek and read");
  for (i = 0; i < (int)got; ++i) silent &= pcm[i] == 0;
  expect(got != 0 && silent, "mp3 silence");
  drmp3_uninit(&mp3);
}

int main(void) {
  test_wav();
  test_flac();
  test_mp3();
  if (failures != 0) {
    fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  puts("dr_libs consumer test passed");
  return 0;
}
