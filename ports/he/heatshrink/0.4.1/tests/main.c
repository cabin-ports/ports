/* Encodes and decodes upstream's test vectors (test_heatshrink_dynamic.c),
 * checks the allocators' and sinks' argument handling, and streams text and
 * pseudo-random bytes through the encoder and decoder in small chunks. */
#include <heatshrink_decoder.h>
#include <heatshrink_encoder.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

static void check(int ok, const char *what) {
  if (!ok) {
    fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

/* Encodes INPUT with one sink, as upstream's vector tests do; returns the
 * number of bytes written to OUT. */
static size_t encode_once(uint8_t window_sz2, uint8_t lookahead_sz2,
                          const char *input, uint8_t *out, size_t out_size) {
  heatshrink_encoder *hse = heatshrink_encoder_alloc(window_sz2, lookahead_sz2);
  uint8_t in[64];
  size_t n = strlen(input);
  size_t count = 0, written = 0;

  check(hse != NULL, "encoder_alloc");
  if (hse == NULL) {
    return 0;
  }
  memcpy(in, input, n);
  check(heatshrink_encoder_sink(hse, in, n, &count) == HSER_SINK_OK &&
            count == n,
        "encoder_sink takes the whole input");
  /* The encoder holds back input until it is told the input is complete. */
  check(heatshrink_encoder_poll(hse, out, out_size, &count) ==
                HSER_POLL_EMPTY &&
            count == 0,
        "no output before finish");
  check(heatshrink_encoder_finish(hse) == HSER_FINISH_MORE, "finish: more");
  check(heatshrink_encoder_poll(hse, out, out_size, &written) ==
            HSER_POLL_EMPTY,
        "encoder_poll drains");
  check(heatshrink_encoder_finish(hse) == HSER_FINISH_DONE, "finish: done");
  check(heatshrink_encoder_sink(hse, in, 1, &count) == HSER_SINK_ERROR_MISUSE,
        "sink after finish is misuse");
  heatshrink_encoder_free(hse);
  return written;
}

static void test_encoder_vectors(void) {
  static const uint8_t aaaaa[] = {0xb0, 0x80, 0x01, 0x80};
  static const uint8_t abcdabcde[] = {0xb0, 0xd8, 0xac, 0x76,
                                      0x40, 0x1b, 0xb2, 0x80};
  uint8_t out[64];
  size_t n;

  /* A literal "a", then a backreference one byte back for four bytes. */
  n = encode_once(8, 7, "aaaaa", out, sizeof out);
  check(n == sizeof aaaaa && memcmp(out, aaaaa, n) == 0, "encode aaaaa");
  /* "abcd", a backreference to it, and a trailing literal "e". */
  n = encode_once(8, 3, "abcdabcde", out, sizeof out);
  check(n == sizeof abcdabcde && memcmp(out, abcdabcde, n) == 0,
        "encode abcdabcde");
}

/* Decodes INPUT into a 16-byte buffer, which must hold all of it, and checks
 * that the decoder then reports the stream complete. */
static void check_decode(uint8_t window_sz2, uint8_t lookahead_sz2,
                         const uint8_t *input, size_t size,
                         const char *expected, const char *what) {
  heatshrink_decoder *hsd = heatshrink_decoder_alloc(256, window_sz2,
                                                     lookahead_sz2);
  uint8_t in[16];
  uint8_t out[16];
  size_t count = 0;
  size_t n = strlen(expected);

  check(hsd != NULL, "decoder_alloc");
  if (hsd == NULL) {
    return;
  }
  memcpy(in, input, size);
  check(heatshrink_decoder_sink(hsd, in, size, &count) == HSDR_SINK_OK &&
            count == size,
        "decoder_sink");
  check(heatshrink_decoder_poll(hsd, out, sizeof out, &count) ==
                HSDR_POLL_EMPTY &&
            count == n && memcmp(out, expected, n) == 0,
        what);
  /* What is left is the zero padding of the last byte. */
  check(heatshrink_decoder_finish(hsd) == HSDR_FINISH_DONE, "decoder done");
  heatshrink_decoder_free(hsd);
}

static void test_decoder_vectors(void) {
  static const uint8_t foofoo[] = {0xb3, 0x5b, 0xed, 0xe0, 0x41, 0x00};
  static const uint8_t aaaaa[] = {0xb0, 0x80, 0x01, 0x80};

  check_decode(7, 6, foofoo, sizeof foofoo, "foofoo", "decode foofoo");
  /* A backreference that overlaps the bytes it produces. */
  check_decode(8, 7, aaaaa, sizeof aaaaa, "aaaaa", "decode aaaaa");
}

static void test_arguments(void) {
  heatshrink_decoder *hsd;
  uint8_t in[10] = {0};
  uint8_t out[4];
  size_t count = 0;

  check(heatshrink_encoder_alloc(3, 2) == NULL, "window below 4 bits");
  check(heatshrink_encoder_alloc(16, 4) == NULL, "window above 15 bits");
  check(heatshrink_encoder_alloc(8, 2) == NULL, "lookahead below 3 bits");
  check(heatshrink_encoder_alloc(8, 8) == NULL, "lookahead as large as window");
  check(heatshrink_decoder_alloc(0, 8, 4) == NULL, "empty input buffer");
  check(heatshrink_decoder_alloc(64, 8, 8) == NULL,
        "decoder lookahead as large as window");

  check(heatshrink_encoder_sink(NULL, in, 1, &count) == HSER_SINK_ERROR_NULL,
        "encoder_sink rejects NULL");
  check(heatshrink_encoder_poll(NULL, out, sizeof out, &count) ==
            HSER_POLL_ERROR_NULL,
        "encoder_poll rejects NULL");
  check(heatshrink_encoder_finish(NULL) == HSER_FINISH_ERROR_NULL,
        "encoder_finish rejects NULL");
  check(heatshrink_decoder_poll(NULL, out, sizeof out, &count) ==
            HSDR_POLL_ERROR_NULL,
        "decoder_poll rejects NULL");

  /* The decoder takes what fits in its input buffer and then reports it
   * full. */
  hsd = heatshrink_decoder_alloc(4, 8, 4);
  check(hsd != NULL, "decoder_alloc with a 4-byte input buffer");
  if (hsd != NULL) {
    check(heatshrink_decoder_sink(hsd, in, sizeof in, &count) ==
                  HSDR_SINK_OK &&
              count == 4,
          "decoder_sink takes what fits");
    check(heatshrink_decoder_sink(hsd, in, sizeof in, &count) ==
                  HSDR_SINK_FULL &&
              count == 0,
          "decoder_sink when full");
    heatshrink_decoder_free(hsd);
  }
}

static size_t min_size(size_t a, size_t b) { return a < b ? a : b; }

/* Polls HSE, at most 13 bytes at a time, until it reports EMPTY. */
static int drain(heatshrink_encoder *hse, uint8_t *comp, size_t cap,
                 size_t *size, int *polled_more) {
  HSE_poll_res pres;
  size_t count;

  do {
    if (*size == cap) {
      return 0;
    }
    pres = heatshrink_encoder_poll(hse, &comp[*size],
                                   min_size(cap - *size, 13), &count);
    *size += count;
    *polled_more |= pres == HSER_POLL_MORE;
  } while (pres == HSER_POLL_MORE);
  return pres == HSER_POLL_EMPTY;
}

/* Streams INPUT through the encoder and then the decoder, sinking at most 100
 * bytes and polling at most 13 at a time; returns the compressed size, or 0
 * if the round trip fails. */
static size_t round_trip(const uint8_t *input, size_t size, int *polled_more) {
  /* Room for the worst case, 9 bits for each literal byte. */
  const size_t comp_cap = size * 2 + 16, decomp_cap = size + 16;
  heatshrink_encoder *hse = heatshrink_encoder_alloc(11, 4);
  heatshrink_decoder *hsd = heatshrink_decoder_alloc(64, 11, 4);
  uint8_t *comp = malloc(comp_cap);
  uint8_t *decomp = malloc(decomp_cap);
  size_t sunk = 0, comp_size = 0, decomp_size = 0, count;
  size_t result = 0;
  HSE_finish_res fres = HSER_FINISH_ERROR_NULL;
  int ok = hse != NULL && hsd != NULL && comp != NULL && decomp != NULL;

  while (ok && sunk < size) {
    ok = heatshrink_encoder_sink(hse, (uint8_t *)&input[sunk],
                                 min_size(size - sunk, 100),
                                 &count) == HSER_SINK_OK;
    sunk += count;
    ok = ok && drain(hse, comp, comp_cap, &comp_size, polled_more);
  }
  /* Poll until finish reports DONE, as heatshrink_encoder.h:104-106 asks:
   * poll returns EMPTY, not MORE, when its buffer has no room left for the
   * last partial byte (heatshrink_encoder.c:238-240). */
  while (ok && (fres = heatshrink_encoder_finish(hse)) == HSER_FINISH_MORE) {
    ok = drain(hse, comp, comp_cap, &comp_size, polled_more);
  }
  ok = ok && fres == HSER_FINISH_DONE;

  sunk = 0;
  while (ok && sunk < comp_size) {
    HSD_poll_res pres;

    ok = heatshrink_decoder_sink(hsd, &comp[sunk], comp_size - sunk,
                                 &count) == HSDR_SINK_OK;
    sunk += count;
    do {
      pres = heatshrink_decoder_poll(hsd, &decomp[decomp_size],
                                     min_size(decomp_cap - decomp_size, 13),
                                     &count);
      decomp_size += count;
    } while (ok && pres == HSDR_POLL_MORE && decomp_size < decomp_cap);
    ok = ok && pres == HSDR_POLL_EMPTY;
  }
  ok = ok && heatshrink_decoder_finish(hsd) == HSDR_FINISH_DONE;

  if (ok && decomp_size == size && memcmp(decomp, input, size) == 0) {
    result = comp_size;
  }
  free(comp);
  free(decomp);
  if (hse != NULL) {
    heatshrink_encoder_free(hse);
  }
  if (hsd != NULL) {
    heatshrink_decoder_free(hsd);
  }
  return result;
}

static void test_round_trips(void) {
  static const char sentence[] =
      "heatshrink compresses data for embedded systems. ";
  enum { size = 5000 };
  uint8_t *data = malloc(size);
  uint32_t seed = 12345;
  size_t i, comp_size;
  int polled_more = 0;

  check(data != NULL, "malloc");
  if (data == NULL) {
    return;
  }
  for (i = 0; i < size; ++i) {
    data[i] = (uint8_t)sentence[i % (sizeof sentence - 1)];
  }
  comp_size = round_trip(data, size, &polled_more);
  check(comp_size != 0, "round trip of repeated text");
  /* Each backreference takes 16 bits for up to 16 bytes. */
  check(comp_size < size / 4, "repeated text compresses");
  check(polled_more, "encoder_poll fills its output buffer");

  for (i = 0; i < size; ++i) {
    seed = seed * 1103515245u + 12345u;
    data[i] = (uint8_t)(seed >> 16);
  }
  comp_size = round_trip(data, size, &polled_more);
  check(comp_size != 0, "round trip of pseudo-random bytes");

  /* Twelve literals take 108 bits: a 13-byte poll fills before the last
   * partial byte, which the next round of finish and poll writes. */
  memcpy(data, "abcdefghijkl", 12);
  check(round_trip(data, 12, &polled_more) == 14,
        "round trip with a last byte pending");
  free(data);
}

int main(void) {
  test_encoder_vectors();
  test_decoder_vectors();
  test_arguments();
  test_round_trips();
  if (failures == 0) {
    puts("heatshrink: ok");
  }
  return failures == 0 ? 0 : 1;
}
