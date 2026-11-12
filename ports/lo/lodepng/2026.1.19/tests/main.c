/* Encodes an RGBA image to PNG in memory, decodes it back and checks the
   pixels, reads the header alone, and checks that a bad chunk CRC and a
   truncated stream are rejected.  C89, as the probe compiles it. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lodepng.h"

#define W 5
#define H 3

static int fail(const char *what, unsigned error) {
  fprintf(stderr, "%s: %u %s\n", what, error, lodepng_error_text(error));
  return 1;
}

int main(void) {
  static const unsigned char signature[8] = {137, 80, 78, 71, 13, 10, 26, 10};
  unsigned char pixels[W * H * 4];
  unsigned char *png = NULL;
  unsigned char *decoded = NULL;
  size_t png_size = 0;
  unsigned w = 0, h = 0, error;
  LodePNGState state;
  size_t i;

  for (i = 0; i < sizeof pixels; ++i) {
    pixels[i] = (unsigned char)(i * 37u + 11u);
  }

  error = lodepng_encode32(&png, &png_size, pixels, W, H);
  if (error) return fail("encode", error);
  if (png_size < 33 || memcmp(png, signature, sizeof signature) != 0) {
    fprintf(stderr, "no PNG signature\n");
    return 1;
  }

  error = lodepng_decode32(&decoded, &w, &h, png, png_size);
  if (error) return fail("decode", error);
  if (w != W || h != H || memcmp(decoded, pixels, sizeof pixels) != 0) {
    fprintf(stderr, "round trip changed the image\n");
    return 1;
  }
  free(decoded);
  decoded = NULL;

  lodepng_state_init(&state);
  w = h = 0;
  error = lodepng_inspect(&w, &h, &state, png, png_size);
  if (error) return fail("inspect", error);
  if (w != W || h != H) {
    fprintf(stderr, "inspect read %ux%u\n", w, h);
    return 1;
  }
  lodepng_state_cleanup(&state);

  /* Byte 29 starts IHDR's CRC; only the CRC check can reject this. */
  png[29] ^= 0xffu;
  error = lodepng_decode32(&decoded, &w, &h, png, png_size);
  free(decoded);
  decoded = NULL;
  if (error == 0) {
    fprintf(stderr, "corrupted IHDR CRC decoded\n");
    return 1;
  }
  png[29] ^= 0xffu;

  error = lodepng_decode32(&decoded, &w, &h, png, png_size / 2);
  free(decoded);
  if (error == 0) {
    fprintf(stderr, "truncated PNG decoded\n");
    return 1;
  }

  free(png);
  printf("lodepng %s: %ux%u RGBA round trip ok\n", LODEPNG_VERSION_STRING, W, H);
  return 0;
}
