#define SDF_IMPLEMENTATION
#include <sdf.h>

#include <stdio.h>
#include <string.h>

#define N 8
static int failures = 0;

#define CHECK(cond) \
  do { \
    if (!(cond)) { \
      ++failures; \
      fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
    } \
  } while (0)

/* A 4x4 opaque square at x, y in 2..5 of an 8x8 image, symmetric under
 * every flip and under transposition. */
static void square(unsigned char *img) {
  int x, y;
  memset(img, 0, N * N);
  for (y = 2; y <= 5; ++y)
    for (x = 2; x <= 5; ++x) img[x + y * N] = 255;
}

static int inside(int x, int y) { return x >= 2 && x <= 5 && y >= 2 && y <= 5; }

int main(void) {
  unsigned char img[N * N], out[N * N], noalloc[N * N], inplace[N * N];
  static float temp[N * N * 3];
  int x, y;

  square(img);

  /* sdfBuildDistanceField, radius 2: 0 = 2 pixels outside, 255 = 2 inside.
   * Pixel centres beside an axis-aligned edge are 0.5 from it, so the
   * outside ones encode 0.5 - (0.5/2)/2 = 0.375 (95) and the inside ones
   * 0.625 (159); the four central pixels are 1.5 from the edge: 0.875
   * (223). */
  CHECK(sdfBuildDistanceField(out, N, 2.0f, img, N, N, N) == 1);
  for (y = 0; y < N; ++y)
    for (x = 0; x < N; ++x) {
      if (x == 0 || y == 0 || x == N - 1 || y == N - 1)
        CHECK(out[x + y * N] == 0); /* the border is not calculated */
      else if (inside(x, y))
        CHECK(out[x + y * N] > 127);
      else
        CHECK(out[x + y * N] < 128);
    }
  CHECK(out[1 + 3 * N] == 95 && out[1 + 4 * N] == 95);
  CHECK(out[6 + 3 * N] == 95 && out[6 + 4 * N] == 95);
  CHECK(out[3 + 1 * N] == 95 && out[4 + 1 * N] == 95);
  CHECK(out[3 + 6 * N] == 95 && out[4 + 6 * N] == 95);
  CHECK(out[2 + 3 * N] == 159 && out[5 + 4 * N] == 159);
  CHECK(out[3 + 2 * N] == 159 && out[4 + 5 * N] == 159);
  CHECK(out[3 + 3 * N] == 223 && out[4 + 3 * N] == 223);
  CHECK(out[3 + 4 * N] == 223 && out[4 + 4 * N] == 223);
  /* Farther outside is lower: the corner pixel is beyond the edge pixels. */
  CHECK(out[1 + 1 * N] < out[1 + 3 * N]);

  /* The result keeps the input's symmetries. */
  for (y = 0; y < N; ++y)
    for (x = 0; x < N; ++x) {
      CHECK(out[x + y * N] == out[(N - 1 - x) + y * N]);
      CHECK(out[x + y * N] == out[x + (N - 1 - y) * N]);
      CHECK(out[x + y * N] == out[y + x * N]);
    }

  /* No-alloc variant and in-place use produce the same bytes. */
  sdfBuildDistanceFieldNoAlloc(noalloc, N, 2.0f, img, N, N, N,
                               (unsigned char *)temp);
  CHECK(memcmp(noalloc, out, sizeof out) == 0);
  memcpy(inplace, img, sizeof img);
  CHECK(sdfBuildDistanceField(inplace, N, 2.0f, inplace, N, N, N) == 1);
  CHECK(memcmp(inplace, out, sizeof out) == 0);

  /* A radius twice as large halves the distances: 0.5 -/+ (0.5/4)/2 beside
   * the edge, outside (111) and inside (143). */
  CHECK(sdfBuildDistanceField(out, N, 4.0f, img, N, N, N) == 1);
  CHECK(out[1 + 3 * N] == 111 && out[2 + 3 * N] == 143);

  /* sdfCoverageToDistanceField: opaque pixels are 255 and pixels away from
   * the shape 0. */
  memset(out, 7, sizeof out);
  sdfCoverageToDistanceField(out, N, img, N, N, N);
  for (y = 0; y < N; ++y)
    for (x = 0; x < N; ++x) {
      if (x == 0 || y == 0 || x == N - 1 || y == N - 1)
        CHECK(out[x + y * N] == 0);
      else if (inside(x, y))
        CHECK(out[x + y * N] == 255);
      else
        CHECK(out[x + y * N] < 128);
    }
  CHECK(out[1 + 1 * N] == 0); /* touches no opaque pixel */
  CHECK(out[1 + 3 * N] == 37); /* (0.5 - 0.5 / sqrt(2)) * 255 */

  /* Coverage 128/255 is just past the half-way point of the edge (127), and
   * 51/255 lies 0.3 pixel outside it: 0.5 - 0.3/sqrt(2) (73). */
  square(img);
  img[1 + 3 * N] = 128;
  sdfCoverageToDistanceField(out, N, img, N, N, N);
  CHECK(out[1 + 3 * N] == 127);
  square(img);
  img[1 + 4 * N] = 51;
  sdfCoverageToDistanceField(out, N, img, N, N, N);
  CHECK(out[1 + 4 * N] == 73);

  if (failures) {
    fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  puts("memononen-sdf ok");
  return 0;
}
