/* Compresses compressible and incompressible inputs through the one-call API
 * and the block API, then checks that every byte comes back and that damaged
 * input is rejected. */
#include <libbz3.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { N = 150000 };
static uint8_t in[N], packed[N + N / 50 + 64], back[N];

static int fail(const char *what) {
  fprintf(stderr, "bzip3 C test failed: %s\n", what);
  return 0;
}

static int round_trip(const char *what, size_t n, int compressible) {
  size_t i, packed_size = sizeof packed, back_size = sizeof back;
  uint32_t seed = 12345;
  for (i = 0; i < n; i++) {
    seed = seed * 1664525u + 1013904223u;
    in[i] = compressible ? (uint8_t)(i * 7 % 251) : (uint8_t)(seed >> 24);
  }
  if (bz3_compress(1 << 20, in, packed, n, &packed_size) != BZ3_OK)
    return fail(what);
  if (memcmp(packed, "BZ3v1", 5) != 0) return fail("stream signature");
  if (compressible && packed_size >= n / 4) return fail("compressible size");
  if (bz3_decompress(packed, back, packed_size, &back_size) != BZ3_OK)
    return fail(what);
  return back_size == n && memcmp(back, in, n) == 0 ? 1 : fail(what);
}

static int damage_is_rejected(void) {
  size_t packed_size = sizeof packed, back_size = sizeof back;
  size_t i;
  for (i = 0; i < 1000; i++) in[i] = (uint8_t)(i % 13);
  if (bz3_compress(1 << 20, in, packed, 1000, &packed_size) != BZ3_OK)
    return fail("small compress");
  if (bz3_decompress(packed, back, packed_size - 1, &back_size) !=
      BZ3_ERR_TRUNCATED_DATA)
    return fail("truncated stream");
  /* Bytes 13..20 are the block's sizes; the CRC-32 of the data follows. */
  packed[21] ^= 0x55;
  back_size = sizeof back;
  if (bz3_decompress(packed, back, packed_size, &back_size) != BZ3_ERR_CRC)
    return fail("damaged checksum");
  packed[21] ^= 0x55;
  packed[0] = 'X';
  back_size = sizeof back;
  if (bz3_decompress(packed, back, packed_size, &back_size) !=
      BZ3_ERR_MALFORMED_HEADER)
    return fail("bad signature");
  return 1;
}

#ifndef _WIN32
static int parallel_blocks(void) {
  enum { BLOCK = 70000 };
  struct bz3_state *states[2];
  uint8_t *bufs[2], *orig[2];
  int32_t sizes[2], orig_sizes[2];
  size_t buf_sizes[2];
  int i, ok = 1;
  for (i = 0; i < 2; i++) {
    size_t j;
    states[i] = bz3_new(BLOCK);
    bufs[i] = malloc(bz3_bound(BLOCK));
    orig[i] = malloc(BLOCK);
    if (!states[i] || !bufs[i] || !orig[i]) return fail("allocation");
    for (j = 0; j < BLOCK; j++) orig[i][j] = (uint8_t)((j * (i + 3)) % 241);
    memcpy(bufs[i], orig[i], BLOCK);
    sizes[i] = orig_sizes[i] = BLOCK;
    buf_sizes[i] = bz3_bound(BLOCK);
  }
  bz3_encode_blocks(states, bufs, sizes, 2);
  bz3_decode_blocks(states, bufs, buf_sizes, sizes, orig_sizes, 2);
  for (i = 0; i < 2; i++) {
    if (bz3_last_error(states[i]) != BZ3_OK ||
        memcmp(bufs[i], orig[i], BLOCK) != 0)
      ok = fail("parallel blocks");
    bz3_free(states[i]);
    free(bufs[i]);
    free(orig[i]);
  }
  return ok;
}
#endif

int main(void) {
  int ok = 1;
  if (strcmp(bz3_version(), "1.5.5") != 0) ok = fail("version");
  if (bz3_new(1000) != NULL) ok = fail("block size below the minimum");
  ok = round_trip("compressible", N, 1) && ok;
  ok = round_trip("incompressible", N, 0) && ok;
  ok = round_trip("tiny", 5, 0) && ok;
  ok = damage_is_rejected() && ok;
#ifndef _WIN32
  ok = parallel_blocks() && ok;
#endif
  return ok ? 0 : 1;
}
