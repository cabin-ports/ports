/* A C89 consumer of qoi: encodes small RGBA and RGB images in memory and
 * checks every byte against the QOI specification (one chunk of each kind,
 * negative and wrapping deltas, and a run split at its 62-pixel limit),
 * decodes them back, including into a forced channel count, round-trips a
 * file through qoi_write and qoi_read, and checks that invalid input is
 * rejected. */
#define QOI_IMPLEMENTATION
#include "qoi.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond)                                               \
  do {                                                            \
    if (!(cond)) {                                                \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
      ++failures;                                                 \
    }                                                             \
  } while (0)

/* 4x2 RGBA: the start pixel (0,0,0,255) as a run, then a DIFF, a LUMA, an
 * RGB, an RGBA, an INDEX hit on pixel 1, and a run of two. */
static const unsigned char kPixels[8 * 4] = {
    0,   0,  0,   255, 1,   1,  0,   255, 18, 21, 25, 255, 200, 10, 100, 255,
    200, 10, 100, 128, 1,   1,  0,   255, 1,  1,  0,  255, 1,   1,  0,   255,
};

static const unsigned char kRgba[] = {
    'q',  'o',  'i', 'f', 0, 0, 0, 4, 0, 0, 0, 2, 4, 0,
    0xc0,                    /* run of 1 */
    0x7e,                    /* diff +1 +1 0 */
    0xb4, 0x5d,              /* luma: g +20, r-g -3, b-g +5 */
    0xfe, 200,  10,  100,    /* rgb */
    0xff, 200,  10,  100, 128, /* rgba */
    0x3d,                    /* index 61 */
    0xc1,                    /* run of 2 */
    0,    0,    0,   0,   0, 0, 0, 1,
};

/* The same pixels as linear RGB: pixel 4 now repeats pixel 3. */
static const unsigned char kRgb[] = {
    'q',  'o',  'i',  'f', 0,   0,  0,  4,    0,    0,    0, 2, 3, 1,
    0xc0, 0x7e, 0xb4, 0x5d, 0xfe, 200, 10, 100, 0xc0, 0x3d, 0xc1,
    0,    0,    0,    0,   0,   0,  0,  1,
};

/* 64x1 of the start pixel: runs of 62 and 2. */
static const unsigned char kLongRun[] = {
    'q', 'o', 'i', 'f', 0, 0, 0, 64, 0, 0, 0, 1, 4, 0,
    0xfd, 0xc1, 0, 0, 0, 0, 0, 0, 0, 1,
};

static int same(const void *data, int len, const unsigned char *expected,
                size_t expected_len) {
  return data != NULL && len == (int)expected_len &&
         memcmp(data, expected, expected_len) == 0;
}

static void test_rgba(void) {
  const qoi_desc desc = {4, 2, 4, QOI_SRGB};
  int len = 0;
  unsigned char *encoded = qoi_encode(kPixels, &desc, &len);
  qoi_desc out = {0, 0, 0, 0};
  unsigned char *decoded;
  int rgb_ok;
  int i;
  CHECK(same(encoded, len, kRgba, sizeof kRgba));

  decoded = qoi_decode(encoded, len, &out, 0);
  CHECK(decoded != NULL && memcmp(decoded, kPixels, sizeof kPixels) == 0);
  CHECK(out.width == 4 && out.height == 2 && out.channels == 4 &&
        out.colorspace == QOI_SRGB);
  free(decoded);

  /* Forcing three channels drops alpha; desc still reports the file's. */
  decoded = qoi_decode(encoded, len, &out, 3);
  rgb_ok = decoded != NULL;
  for (i = 0; rgb_ok && i < 8 * 3; ++i) {
    rgb_ok = decoded[i] == kPixels[i / 3 * 4 + i % 3];
  }
  CHECK(rgb_ok);
  CHECK(out.channels == 4);
  free(decoded);
  free(encoded);
}

static void test_rgb(void) {
  const qoi_desc desc = {4, 2, 3, QOI_LINEAR};
  qoi_desc out = {0, 0, 0, 0};
  unsigned char rgb[8 * 3];
  unsigned char *encoded;
  unsigned char *decoded;
  int len = 0;
  int rgba_ok;
  int i;
  for (i = 0; i < 8 * 3; ++i) {
    rgb[i] = kPixels[i / 3 * 4 + i % 3];
  }
  encoded = qoi_encode(rgb, &desc, &len);
  CHECK(same(encoded, len, kRgb, sizeof kRgb));

  decoded = qoi_decode(encoded, len, &out, 0);
  CHECK(decoded != NULL && memcmp(decoded, rgb, sizeof rgb) == 0);
  CHECK(out.width == 4 && out.height == 2 && out.channels == 3 &&
        out.colorspace == QOI_LINEAR);
  free(decoded);

  /* Forcing four channels adds an opaque alpha. */
  decoded = qoi_decode(encoded, len, &out, 4);
  rgba_ok = decoded != NULL;
  for (i = 0; rgba_ok && i < 8 * 4; ++i) {
    rgba_ok = decoded[i] == (i % 4 == 3 ? 255 : rgb[i / 4 * 3 + i % 4]);
  }
  CHECK(rgba_ok);
  free(decoded);
  free(encoded);
}

static void test_long_run(void) {
  const qoi_desc desc = {64, 1, 4, QOI_SRGB};
  qoi_desc out;
  unsigned char pixels[64 * 4];
  unsigned char *encoded;
  unsigned char *decoded;
  int len = 0;
  int i;
  for (i = 0; i < 64 * 4; ++i) {
    pixels[i] = i % 4 == 3 ? 255 : 0;
  }
  encoded = qoi_encode(pixels, &desc, &len);
  CHECK(same(encoded, len, kLongRun, sizeof kLongRun));
  decoded = qoi_decode(encoded, len, &out, 0);
  CHECK(decoded != NULL && memcmp(decoded, pixels, sizeof pixels) == 0);
  free(decoded);
  free(encoded);
}

/* Negative DIFF deltas that wrap 0 -> 255, an INDEX hit on the zeroed
 * table's transparent black, and an RGB chunk once r moves too far. */
static const unsigned char kWrapPixels[4 * 4] = {
    255, 255, 255, 255, 0, 0, 0, 0, 254, 1, 255, 0, 30, 0, 250, 0,
};

static const unsigned char kWrap[] = {
    'q', 'o', 'i', 'f', 0, 0, 0, 4, 0, 0, 0, 1, 4, 0,
    0x55,                /* diff -1 -1 -1 */
    0x00,                /* index 0 */
    0x4d,                /* diff -2 +1 -1 */
    0xfe, 30, 0, 250,    /* rgb */
    0, 0, 0, 0, 0, 0, 0, 1,
};

static void test_wrap(void) {
  const qoi_desc desc = {4, 1, 4, QOI_SRGB};
  qoi_desc out;
  unsigned char *encoded;
  unsigned char *decoded;
  int len = 0;
  encoded = qoi_encode(kWrapPixels, &desc, &len);
  CHECK(same(encoded, len, kWrap, sizeof kWrap));
  decoded = qoi_decode(encoded, len, &out, 0);
  CHECK(decoded != NULL &&
        memcmp(decoded, kWrapPixels, sizeof kWrapPixels) == 0);
  free(decoded);
  free(encoded);
}

static void test_file(void) {
  const qoi_desc desc = {4, 2, 4, QOI_SRGB};
  const char *dir = getenv("CABIN_PROFILE_DIR");
  const char name[] = "/port-probe-qoi-c.qoi";
  char path[4096];
  qoi_desc out = {0, 0, 0, 0};
  unsigned char *decoded;

  if (dir == NULL || strlen(dir) + sizeof name > sizeof path) {
    CHECK(!"CABIN_PROFILE_DIR");
    return;
  }
  strcpy(path, dir);
  strcat(path, name);
  CHECK(qoi_write(path, kPixels, &desc) == (int)sizeof kRgba);
  decoded = qoi_read(path, &out, 0);
  CHECK(decoded != NULL && memcmp(decoded, kPixels, sizeof kPixels) == 0);
  CHECK(out.width == 4 && out.height == 2 && out.channels == 4 &&
        out.colorspace == QOI_SRGB);
  free(decoded);
  CHECK(remove(path) == 0);
}

/* 65536x65536 exceeds QOI_PIXELS_MAX. */
static const unsigned char kHuge[] = {
    'q', 'o', 'i', 'f', 0, 1, 0, 0, 0, 1, 0, 0, 4, 0,
    0, 0, 0, 0, 0, 0, 0, 1,
};

static void test_invalid(void) {
  qoi_desc out;
  qoi_desc desc = {4, 2, 5, QOI_SRGB};
  unsigned char bad[sizeof kRgba];
  int len = 0;
  memcpy(bad, kRgba, sizeof bad);
  bad[0] = 'x';
  CHECK(qoi_decode(bad, (int)sizeof bad, &out, 0) == NULL);
  /* Shorter than the header plus the end marker. */
  CHECK(qoi_decode(kRgba, 21, &out, 0) == NULL);
  CHECK(qoi_decode(kRgba, (int)sizeof kRgba, &out, 2) == NULL);
  CHECK(qoi_decode(kHuge, (int)sizeof kHuge, &out, 0) == NULL);

  CHECK(qoi_encode(kPixels, &desc, &len) == NULL);
  desc.channels = 4;
  desc.colorspace = 2;
  CHECK(qoi_encode(kPixels, &desc, &len) == NULL);
  desc.colorspace = QOI_SRGB;
  desc.width = 0;
  CHECK(qoi_encode(kPixels, &desc, &len) == NULL);
  desc.width = 65536;
  desc.height = 65536;
  CHECK(qoi_encode(kPixels, &desc, &len) == NULL);
}

int main(void) {
  test_rgba();
  test_rgb();
  test_long_run();
  test_wrap();
  test_file();
  test_invalid();
  if (failures != 0) {
    printf("%d failures\n", failures);
    return 1;
  }
  printf("qoi: ok\n");
  return 0;
}
