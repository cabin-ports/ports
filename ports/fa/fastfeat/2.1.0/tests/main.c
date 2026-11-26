/* FAST corner detectors on a 32x32 image: a bright 8x8 square (x and y in
   12..19) and one isolated bright pixel at (26,26) on a black background.

   Expected corners, from the segment test (a pixel is a corner when N
   contiguous pixels of the 16-pixel radius-3 ring are all brighter or all
   darker than it by more than the threshold):
   - the isolated pixel has all 16 ring pixels darker: a corner for N = 9..12;
   - a square corner pixel has 11 contiguous ring pixels on the background:
     a corner for N <= 11, not 12; a mid-edge pixel has 8 at most: never.
   fast9 and fast10 also fire on pixels next to the square corners, whose
   scores tie, so nonmax suppression (ties suppress both) leaves only the
   isolated pixel.  Detection skips a 3-pixel border. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fast.h>

#define W 32
#define THRESHOLD 50

static int failures = 0;

#define CHECK(cond)                                                       \
  do {                                                                    \
    if (!(cond)) {                                                        \
      fprintf(stderr, "main.c:%d: check failed: %s\n", __LINE__, #cond);  \
      ++failures;                                                         \
    }                                                                     \
  } while (0)

typedef xy *(*detect_fn)(const byte *, int, int, int, int, int *);

static const xy square_corners[] = {{12, 12}, {19, 12}, {12, 19}, {19, 19}};

static int same(const xy *got, int n, const xy *want, int m) {
  return n == m && (m == 0 || memcmp(got, want, sizeof(xy) * m) == 0);
}

/* Runs one detector and checks its exact corner list; frees the result. */
static void expect(detect_fn fn, const byte *im, int stride, const xy *want,
                   int m) {
  int n = -1;
  xy *got = fn(im, W, W, stride, THRESHOLD, &n);
  CHECK(same(got, n, want, m));
  free(got);
}

int main(void) {
  static byte im[W * W];
  static byte flat[W * W];
  int x, y;
  xy all[5];
  xy dot[1];
  int n = -1;
  xy *got;

  for (y = 12; y <= 19; ++y)
    for (x = 12; x <= 19; ++x) im[y * W + x] = 255;
  im[26 * W + 26] = 255;
  memset(flat, 128, sizeof flat);

  memcpy(all, square_corners, sizeof square_corners);
  all[4].x = 26;
  all[4].y = 26;
  dot[0] = all[4];

  /* Nonmax-suppressed detection. */
  expect(fast9_detect_nonmax, im, W, dot, 1);
  expect(fast10_detect_nonmax, im, W, dot, 1);
  expect(fast11_detect_nonmax, im, W, all, 5);
  expect(fast12_detect_nonmax, im, W, dot, 1);

  /* Raw detection: the counts shrink as N grows, and the full corner
     list of fast11 is the square corners plus the isolated pixel. */
  got = fast9_detect(im, W, W, W, THRESHOLD, &n);
  CHECK(n == 25);
  free(got);
  got = fast10_detect(im, W, W, W, THRESHOLD, &n);
  CHECK(n == 13);
  free(got);
  expect(fast11_detect, im, W, all, 5);
  expect(fast12_detect, im, W, dot, 1);

  /* A uniform image has no corners. */
  expect(fast9_detect_nonmax, flat, W, NULL, 0);
  expect(fast12_detect_nonmax, flat, W, NULL, 0);
  got = fast9_detect(flat, W, W, W, THRESHOLD, &n);
  CHECK(n == 0);
  free(got);

  /* Scores of the isolated pixel: the largest threshold at which it is
     still a corner, 254 against a 255 contrast. */
  {
    int pixel[16];
    const byte *p = im + 26 * W + 26;
    static const int dx[16] = {0, 1, 2, 3, 3, 3, 2, 1, 0, -1, -2, -3, -3, -3, -2, -1};
    static const int dy[16] = {3, 3, 2, 1, 0, -1, -2, -3, -3, -3, -2, -1, 0, 1, 2, 3};
    int i;
    for (i = 0; i < 16; ++i) pixel[i] = dx[i] + dy[i] * W;
    CHECK(fast9_corner_score(p, pixel, THRESHOLD) == 254);
    CHECK(fast12_corner_score(p, pixel, THRESHOLD) == 254);
  }

  if (failures) return EXIT_FAILURE;
  puts("fastfeat C ok");
  return EXIT_SUCCESS;
}
