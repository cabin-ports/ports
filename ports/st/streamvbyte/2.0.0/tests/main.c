/* Consumer test for cabin-ports/streamvbyte: the exact byte layout of both
   stream formats, round trips of a long mixed-width array through the
   SIMD-sized blocks, stream validation, and the delta and zigzag codecs. */
#include <streamvbyte.h>
#include <streamvbyte_zigzag.h>
#include <streamvbytedelta.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int failures = 0;

static void check(int ok, const char *what) {
  if (!ok) {
    printf("FAILED: %s\n", what);
    ++failures;
  }
}

/* Not a multiple of 4, so the scalar code finishes after the SIMD blocks. */
#define N 1003

static uint32_t values[N];
static uint32_t decoded[N];
/* Decoders may read up to STREAMVBYTE_PADDING bytes past the stream, which
   streamvbyte_max_compressedbytes includes. */
static uint8_t stream[4 * N + (N + 3) / 4 + STREAMVBYTE_PADDING];

static uint32_t next_random(uint32_t *state) {
  *state = *state * 1664525u + 1013904223u;
  return *state;
}

static void layout(void) {
  /* Two blocks of one value of each width: 1, 2, 3 and 4 bytes, so keys
     0, 1, 2, 3; eight values fill a whole x86-64 SIMD encoder block. */
  const uint32_t in[8] = {1, 300, 70000, 0x12345678, 1, 300, 70000, 0x12345678};
  const uint8_t expected[22] = {0xE4, 0xE4,
                                0x01, 0x2C, 0x01, 0x70, 0x11, 0x01, 0x78, 0x56, 0x34, 0x12,
                                0x01, 0x2C, 0x01, 0x70, 0x11, 0x01, 0x78, 0x56, 0x34, 0x12};
  uint32_t out[8] = {0};
  memset(stream, 0, sizeof stream);
  check(streamvbyte_encode(in, 8, stream) == 22, "encode returns the stream size");
  check(memcmp(stream, expected, 22) == 0, "the key bytes, then little-endian data");
  check(streamvbyte_compressedbytes(in, 8) == 22, "compressedbytes is exact");
  check(streamvbyte_decode(stream, out, 8) == 22, "decode returns the bytes read");
  check(memcmp(in, out, sizeof in) == 0, "decode restores the values");

  /* The 0124 format spends 0, 1, 2 or 4 bytes: a zero costs no data. */
  {
    const uint32_t in0124[4] = {0, 200, 40000, 0x12345678};
    const uint8_t expected0124[8] = {0xE4, 0xC8, 0x40, 0x9C,
                                     0x78, 0x56, 0x34, 0x12};
    memset(stream, 0, sizeof stream);
    check(streamvbyte_encode_0124(in0124, 4, stream) == 8, "0124 encode size");
    check(memcmp(stream, expected0124, 8) == 0, "0124 byte layout");
    check(streamvbyte_compressedbytes_0124(in0124, 4) == 8, "0124 compressedbytes");
    check(streamvbyte_decode_0124(stream, out, 4) == 8, "0124 decode size");
    check(memcmp(in0124, out, sizeof in0124) == 0, "0124 decode restores the values");
  }

  check(streamvbyte_max_compressedbytes(4) == 1 + 16 + STREAMVBYTE_PADDING,
        "max_compressedbytes: keys, four bytes per value, and padding");
}

static void round_trips(void) {
  uint32_t state = 12345;
  size_t written, read;
  for (int i = 0; i < N; ++i) {
    uint32_t r = next_random(&state);
    values[i] = r >> (next_random(&state) >> 27); /* every width, and zeros */
  }

  written = streamvbyte_encode(values, N, stream);
  check(written == streamvbyte_compressedbytes(values, N), "long encode size");
  check(written <= streamvbyte_max_compressedbytes(N) - STREAMVBYTE_PADDING,
        "long encode fits the bound");
  memset(decoded, 0, sizeof decoded);
  read = streamvbyte_decode(stream, decoded, N);
  check(read == written, "long decode reads the whole stream");
  check(memcmp(values, decoded, sizeof values) == 0, "long round trip");
  check(streamvbyte_validate_stream(stream, written, N), "a whole stream validates");
  check(!streamvbyte_validate_stream(stream, written - 1, N), "a truncated stream fails");
  check(!streamvbyte_validate_stream(stream, written + 1, N),
        "a stream with a trailing byte fails");
  check(!streamvbyte_validate_stream(stream, written, N + 4), "a wrong count fails");

  written = streamvbyte_encode_0124(values, N, stream);
  check(written == streamvbyte_compressedbytes_0124(values, N), "long 0124 encode size");
  memset(decoded, 0, sizeof decoded);
  check(streamvbyte_decode_0124(stream, decoded, N) == written, "long 0124 decode size");
  check(memcmp(values, decoded, sizeof values) == 0, "long 0124 round trip");
  check(streamvbyte_validate_stream_0124(stream, written, N), "a 0124 stream validates");
  check(!streamvbyte_validate_stream_0124(stream, written + 1, N),
        "a 0124 stream with a trailing byte fails");
}

static void delta(void) {
  size_t written;
  /* Gaps of 3 from prev: each delta takes one byte. */
  for (int i = 0; i < N; ++i)
    values[i] = 1000000u + 3u * (uint32_t)i;
  written = streamvbyte_delta_encode(values, N, stream, 1000000u - 3u);
  check(written == (N + 3) / 4 + N, "deltas from prev take one byte each");
  memset(decoded, 0, sizeof decoded);
  check(streamvbyte_delta_decode(stream, decoded, N, 1000000u - 3u) == written,
        "delta decode size");
  check(memcmp(values, decoded, sizeof values) == 0, "delta round trip");

  /* Gaps of every width, so the general SIMD delta kernels run too. */
  {
    uint32_t state = 7, acc = 0;
    for (int i = 0; i < N; ++i)
      values[i] = acc += next_random(&state) >> (8 * (i % 4));
  }
  written = streamvbyte_delta_encode(values, N, stream, 0);
  memset(decoded, 0, sizeof decoded);
  check(streamvbyte_delta_decode(stream, decoded, N, 0) == written,
        "mixed delta decode size");
  check(memcmp(values, decoded, sizeof values) == 0, "mixed delta round trip");
}

static void zigzag(void) {
  const int32_t in[7] = {0, -1, 1, -2, 2, INT32_MIN, INT32_MAX};
  const uint32_t expected[7] = {0, 1, 2, 3, 4, 0xFFFFFFFFu, 0xFFFFFFFEu};
  const int32_t series[5] = {100, 98, 101, 101, 95};
  const uint32_t expected_delta[5] = {0, 3, 6, 0, 11};
  uint32_t encoded[7];
  int32_t back[7];
  zigzag_encode(in, encoded, 7);
  check(memcmp(encoded, expected, sizeof expected) == 0, "zigzag mapping");
  zigzag_decode(encoded, back, 7);
  check(memcmp(back, in, sizeof in) == 0, "zigzag round trip");
  zigzag_delta_encode(series, encoded, 5, 100);
  check(memcmp(encoded, expected_delta, sizeof expected_delta) == 0,
        "zigzag delta mapping");
  zigzag_delta_decode(encoded, back, 5, 100);
  check(memcmp(back, series, sizeof series) == 0, "zigzag delta round trip");
}

int main(void) {
  layout();
  round_trips();
  delta();
  zigzag();
  printf("streamvbyte (C): %s\n", failures == 0 ? "ok" : "FAILED");
  return failures == 0 ? 0 : 1;
}
