#include <brotli/decode.h>
#include <brotli/encode.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fail(const char *what) {
  fprintf(stderr, "brotli: %s\n", what);
  return 1;
}

/* tests/testdata/ukkonooa.compressed from the upstream archive. */
static const uint8_t kUkkonooa[] = {
    0x1b, 0x76, 0x00, 0x00, 0x14, 0x4a, 0xac, 0x9b, 0x7a, 0xbd, 0xe1, 0x97,
    0x9d, 0x7f, 0x8e, 0xc2, 0x82, 0x36, 0x0e, 0x9c, 0xe0, 0x90, 0x03, 0xf7,
    0x8b, 0x9e, 0x38, 0xe6, 0xb6, 0x00, 0xab, 0xc3, 0xca, 0xa0, 0xc2, 0xda,
    0x66, 0x36, 0xdc, 0xcd, 0x80, 0x8d, 0x2e, 0x21, 0xd7, 0x6e, 0xe3, 0xea,
    0x4c, 0xb8, 0xf0, 0xd2, 0xb8, 0xc7, 0xc2, 0x70, 0x4d, 0x3a, 0xf0, 0x69,
    0x7e, 0xa1, 0xb8, 0x45, 0x73, 0xab, 0xc4, 0x57, 0x1e,
};
static const char kUkkonooaText[] =
    "ukko nooa, ukko nooa oli kunnon mies, kun han meni saunaan, "
    "pisti laukun naulaan, ukko nooa, ukko nooa oli kunnon mies.";

#define SAMPLE 100000

int main(void) {
  static uint8_t input[SAMPLE];
  static uint8_t compressed[SAMPLE];
  static uint8_t output[SAMPLE + 1];
  uint8_t small[256];
  const uint8_t empty_stream = 0x06; /* tests/testdata/empty.compressed */
  size_t small_size = sizeof small;
  size_t compressed_size = sizeof compressed;
  size_t output_size = sizeof output;
  uint32_t seed = 12345;
  size_t i;

  if (BrotliDecoderDecompress(sizeof kUkkonooa, kUkkonooa, &small_size,
                              small) != BROTLI_DECODER_RESULT_SUCCESS ||
      small_size != sizeof kUkkonooaText - 1 ||
      memcmp(small, kUkkonooaText, small_size) != 0)
    return fail("reference stream");

  small_size = sizeof small;
  if (BrotliDecoderDecompress(1, &empty_stream, &small_size, small) !=
          BROTLI_DECODER_RESULT_SUCCESS ||
      small_size != 0)
    return fail("empty stream");

  small_size = sizeof small;
  if (BrotliDecoderDecompress(sizeof kUkkonooa - 1, kUkkonooa, &small_size,
                              small) == BROTLI_DECODER_RESULT_SUCCESS)
    return fail("truncated stream accepted");

  /* Text-like input: words drawn from a small vocabulary. */
  for (i = 0; i < SAMPLE; ++i) {
    seed = seed * 1103515245u + 12345u;
    input[i] = (i % 7 == 6) ? ' ' : (uint8_t)('a' + (seed >> 16) % 6);
  }
  if (!BrotliEncoderCompress(BROTLI_DEFAULT_QUALITY, BROTLI_DEFAULT_WINDOW,
                             BROTLI_MODE_TEXT, SAMPLE, input, &compressed_size,
                             compressed))
    return fail("compress");
  if (compressed_size >= SAMPLE / 2)
    return fail("compressed size");
  if (BrotliDecoderDecompress(compressed_size, compressed, &output_size,
                              output) != BROTLI_DECODER_RESULT_SUCCESS ||
      output_size != SAMPLE || memcmp(output, input, SAMPLE) != 0)
    return fail("round trip");

  if (BrotliEncoderVersion() != 0x1002001 ||
      BrotliDecoderVersion() != 0x1002001)
    return fail("version");
  return 0;
}
