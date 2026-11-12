/* Checks minimp3's decoder on an in-memory stream of silent MPEG-1 Layer III
   frames: one frame through mp3dec_decode_frame, the whole stream through
   minimp3_ex.h's mp3dec_load_buf, and a buffer holding no frame. */
#define MINIMP3_IMPLEMENTATION
#include "minimp3_ex.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 1152 samples at 128 kbit/s and 44.1 kHz, unpadded: 417 bytes. */
enum { FRAME_BYTES = 417, FRAMES = 4, FRAME_SAMPLES = 1152 };

static int failures;

static void expect(int ok, const char* what) {
  if (!ok) {
    fprintf(stderr, "failed: %s\n", what);
    ++failures;
  }
}

int main(void) {
  /* MPEG-1 Layer III without CRC, 128 kbit/s, 44.1 kHz, mono.  All-zero side
     information and main data code silence. */
  static const uint8_t header[4] = {0xFF, 0xFB, 0x90, 0xC0};
  static uint8_t stream[FRAME_BYTES * FRAMES];
  static mp3d_sample_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
  static const uint8_t no_frame[600] = {0};
  mp3dec_t dec;
  mp3dec_frame_info_t info;
  mp3dec_file_info_t file;
  int samples, silent = 1;

  for (int i = 0; i < FRAMES; ++i)
    memcpy(stream + i * FRAME_BYTES, header, sizeof header);

  memset(pcm, 0x55, sizeof pcm); /* so silence must be written */
  mp3dec_init(&dec);
  samples = mp3dec_decode_frame(&dec, stream, (int)sizeof stream, pcm, &info);
  expect(samples == FRAME_SAMPLES && info.frame_bytes == FRAME_BYTES &&
             info.frame_offset == 0 && info.channels == 1 &&
             info.hz == 44100 && info.layer == 3 && info.bitrate_kbps == 128,
         "frame header");
  for (int i = 0; i < samples; ++i) silent &= pcm[i] == 0;
  expect(silent, "silent frame");

  expect(mp3dec_load_buf(&dec, stream, sizeof stream, &file, NULL, NULL) ==
                 0 &&
             file.samples == (size_t)FRAME_SAMPLES * FRAMES &&
             file.channels == 1 && file.hz == 44100 && file.layer == 3 &&
             file.avg_bitrate_kbps == 128,
         "whole stream");
  free(file.buffer);

  /* With no sync word, nothing decodes and every byte counts as skipped. */
  mp3dec_init(&dec);
  expect(mp3dec_decode_frame(&dec, no_frame, (int)sizeof no_frame, pcm,
                             &info) == 0 &&
             info.frame_bytes == (int)sizeof no_frame,
         "no frame");

  if (failures != 0) return 1;
  puts("minimp3 C OK");
  return 0;
}
