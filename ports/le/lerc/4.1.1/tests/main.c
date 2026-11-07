/* The C consumer: a masked float raster encoded with a 0.01 error bound
 * and decoded within it, the blob's header and info, a too-small output
 * buffer, a truncated and a corrupted blob rejected, a blob in the older
 * codec version 2, two bands with masks of their own, and a lossless
 * multi-band RGB round trip. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <Lerc_c_api.h>

/* Lerc_types.h is C++; these are its values. */
enum { dt_uchar = 1, dt_float = 6 };
enum { err_ok = 0, err_buffer_too_small = 3 };
enum { info_size = 11, range_size = 3 };

enum { cols = 13, rows = 11, pixels = cols * rows };

static int ok = 1;

static int check(int cond, const char *what) {
    if (!cond) {
        printf("FAIL: %s\n", what);
        ok = 0;
    }
    return cond;
}

static float raster[pixels];
static unsigned char mask[pixels];
static unsigned char blob[8192];

static int make_raster(float *zmin, float *zmax) {
    int r, c, valid = 0;
    for (r = 0; r < rows; ++r) {
        for (c = 0; c < cols; ++c) {
            int i = r * cols + c;
            raster[i] = 100.0f + 0.37f * c - 0.21f * r + (float) ((c * r) % 7) * 0.013f;
            mask[i] = (c + 2 * r) % 9 != 0;
            if (mask[i]) {
                if (valid == 0 || raster[i] < *zmin) *zmin = raster[i];
                if (valid == 0 || raster[i] > *zmax) *zmax = raster[i];
                ++valid;
            }
        }
    }
    return valid;
}

/* Every mask byte kept and every valid value within the bound (a NaN is
 * not). */
static int within(const float *out, const unsigned char *out_mask, double bound) {
    int i;
    for (i = 0; i < pixels; ++i) {
        if (out_mask[i] != mask[i]) return 0;
        if (out_mask[i] && !(fabs((double) out[i] - (double) raster[i]) <= bound)) return 0;
    }
    return 1;
}

static void float_raster(void) {
    const double max_error = 0.01;
    /* A decoded float is rounded once more, by at most half an ulp near
     * 100. */
    const double bound = max_error + 1e-5;
    float zmin = 0, zmax = 0, out[pixels];
    double out_double[pixels], range[range_size], lo, hi;
    unsigned char out_mask[pixels], corrupted[sizeof blob];
    unsigned int size = 0, written = 0, info[info_size];
    int valid = make_raster(&zmin, &zmax), i, lossy = 0, doubles = 1;

    if (!check(lerc_computeCompressedSize(raster, dt_float, 1, cols, rows, 1, 1, mask, max_error, &size) == err_ok &&
                   size > 0 && size <= sizeof blob,
               "the compressed size is computed"))
        return;
    if (!check(lerc_encode(raster, dt_float, 1, cols, rows, 1, 1, mask, max_error, blob, size, &written) == err_ok &&
                   written == size,
               "the raster encodes into exactly that size"))
        return;
    check(lerc_encode(raster, dt_float, 1, cols, rows, 1, 1, mask, max_error, blob, size - 1, &written) ==
              err_buffer_too_small,
          "a buffer one byte short is too small");
    if (!check(lerc_encode(raster, dt_float, 1, cols, rows, 1, 1, mask, max_error, blob, size, &written) == err_ok,
               "the raster encodes again"))
        return;
    check(memcmp(blob, "Lerc2 ", 6) == 0, "the blob starts with Lerc2's file key");

    if (!check(lerc_getBlobInfo(blob, size, info, range, info_size, range_size) == err_ok, "the blob info is read"))
        return;
    check(info[0] == 6 && info[1] == dt_float && info[2] == 1 && info[3] == cols && info[4] == rows &&
              info[5] == 1 && info[6] == (unsigned int) valid && info[7] == size && info[8] == 1 &&
              info[9] == 1 && info[10] == 0,
          "codec version 6, float, depth 1, 13 x 11, 1 band, the valid pixels, the size, 1 mask, no noData");
    check(range[0] == zmin && range[1] == zmax, "the blob carries the valid range");
    /* Lerc raises the bound only for values on a coarser grid
     * (TryRaiseMaxZError), which these are not. */
    check(range[2] == max_error, "the blob records the error bound asked for");
    check(lerc_getDataRanges(blob, size, 1, 1, &lo, &hi) == err_ok && lo == zmin && hi == zmax,
          "the data ranges agree");

    memset(out_mask, 0xff, sizeof out_mask);
    if (!check(lerc_decode(blob, size, 1, out_mask, 1, cols, rows, 1, dt_float, out) == err_ok,
               "the raster decodes"))
        return;
    check(within(out, out_mask, bound), "the raster decodes within the error bound with its mask");
    for (i = 0; i < pixels; ++i) {
        if (mask[i] && out[i] != raster[i]) lossy = 1;
    }
    check(lossy, "the encoding is lossy");

    memset(out_mask, 0xff, sizeof out_mask);
    if (!check(lerc_decodeToDouble(blob, size, 1, out_mask, 1, cols, rows, 1, out_double) == err_ok,
               "the raster decodes to double"))
        return;
    for (i = 0; i < pixels; ++i) {
        if (out_mask[i] != mask[i] || (mask[i] && out_double[i] != (double) out[i])) doubles = 0;
    }
    check(doubles, "the doubles are the decoded floats");

    check(lerc_decode(blob, size - 1, 1, out_mask, 1, cols, rows, 1, dt_float, out) != err_ok,
          "a truncated blob is rejected");
    memcpy(corrupted, blob, size);
    corrupted[size - 1] ^= 0x55;
    check(lerc_decode(corrupted, size, 1, out_mask, 1, cols, rows, 1, dt_float, out) != err_ok,
          "a corrupted blob fails its checksum");

    /* Codec version 2, from Lerc 2.2, has no checksum. */
    if (!check(lerc_encodeForVersion(raster, 2, dt_float, 1, cols, rows, 1, 1, mask, max_error, blob, sizeof blob,
                                     &size) == err_ok &&
                   lerc_getBlobInfo(blob, size, info, range, info_size, range_size) == err_ok && info[0] == 2,
               "a blob in codec version 2"))
        return;
    memset(out_mask, 0xff, sizeof out_mask);
    check(lerc_decode(blob, size, 1, out_mask, 1, cols, rows, 1, dt_float, out) == err_ok &&
              within(out, out_mask, bound),
          "the version 2 blob decodes within the error bound");
}

/* The raster and its negation as two bands, each with its own mask. */
static void band_masks(void) {
    static float bands[2 * pixels], out[2 * pixels];
    static unsigned char masks[2 * pixels], out_masks[2 * pixels];
    unsigned int size = 0, info[info_size];
    double range[range_size];
    float zmin = 0, zmax = 0;
    int i, same = 1;

    make_raster(&zmin, &zmax);
    for (i = 0; i < pixels; ++i) {
        bands[i] = raster[i];
        masks[i] = mask[i];
        bands[pixels + i] = -raster[i];
        masks[pixels + i] = i % 4 != 1;
    }
    if (!check(lerc_encode(bands, dt_float, 1, cols, rows, 2, 2, masks, 0.01, blob, sizeof blob, &size) == err_ok,
               "two bands with two masks encode"))
        return;
    check(lerc_getBlobInfo(blob, size, info, range, info_size, range_size) == err_ok && info[5] == 2 && info[8] == 2,
          "the blob has 2 bands and 2 masks");
    memset(out_masks, 0xff, sizeof out_masks);
    if (!check(lerc_decode(blob, size, 2, out_masks, 1, cols, rows, 2, dt_float, out) == err_ok,
               "the two bands decode"))
        return;
    for (i = 0; i < 2 * pixels; ++i) {
        if (out_masks[i] != masks[i] || (masks[i] && !(fabs((double) out[i] - (double) bands[i]) <= 0.01 + 1e-5)))
            same = 0;
    }
    check(same, "each band keeps its own mask and its values within the error bound");
}

/* Two bands of 16 x 9 RGB pixels, lossless. */
static void rgb(void) {
    enum { w = 16, h = 9, depth = 3, bands = 2, values = w * h * depth * bands };
    static unsigned char image[values], out[values];
    unsigned int size = 0, info[info_size];
    double range[range_size];
    int b, r, c, d;

    for (b = 0; b < bands; ++b)
        for (r = 0; r < h; ++r)
            for (c = 0; c < w; ++c)
                for (d = 0; d < depth; ++d)
                    image[((b * h + r) * w + c) * depth + d] = (unsigned char) ((c * 7 + r * 13 + d * 29 + b * 31) & 0xff);

    if (!check(lerc_encode(image, dt_uchar, depth, w, h, bands, 0, NULL, 0.5, blob, sizeof blob, &size) == err_ok,
               "the RGB image encodes"))
        return;
    check(lerc_getBlobInfo(blob, size, info, range, info_size, range_size) == err_ok && info[1] == dt_uchar &&
              info[2] == depth && info[3] == w && info[4] == h && info[5] == bands && info[6] == w * h &&
              info[8] == 0,
          "uchar, depth 3, 16 x 9, 2 bands, all valid, no mask");
    check(lerc_decode(blob, size, 0, NULL, depth, w, h, bands, dt_uchar, out) == err_ok &&
              memcmp(image, out, sizeof image) == 0,
          "the RGB image round-trips exactly");
}

int main(void) {
    check(LERC_VERSION_NUMBER == 40101 && LERC_AT_LEAST_VERSION(4, 1, 1) && !LERC_AT_LEAST_VERSION(4, 1, 2),
          "the headers are 4.1.1");
    float_raster();
    band_masks();
    rgb();
    if (!ok) return 1;
    puts("lerc (C): ok");
    return 0;
}
