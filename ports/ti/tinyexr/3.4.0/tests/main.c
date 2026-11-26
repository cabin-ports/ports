/* The C consumer: the v3 API writes an RGBA float image to memory as ZIP and
 * ZSTD, reads it back exactly, decodes it again under both zlib backends,
 * and rejects damaged streams.  The calls follow examples/wasm/exr_wasm.c's
 * exrw_encode_rgba and exrw_decode_rgba. */
#include <exr.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__,        \
                    __LINE__, #cond);                                     \
            exit(1);                                                      \
        }                                                                 \
    } while (0)

enum { W = 32, H = 16, N = W * H };
static const char *const names[4] = {"R", "G", "B", "A"};

static float pixel(int c, int i) {
    int x = i % W, y = i / W;
    float v[4];
    v[0] = (float)x;
    v[1] = (float)y;
    v[2] = 0.25f * (float)(x + y);
    v[3] = 1.0f;
    return v[c];
}

static void *encode(exr_compression comp, size_t *size) {
    exr_image img;
    exr_part part;
    exr_channel chans[4];
    float *plane[4];
    void *out = NULL;
    int c, i;

    memset(&img, 0, sizeof(img));
    memset(&part, 0, sizeof(part));
    memset(chans, 0, sizeof(chans));
    for (c = 0; c < 4; ++c) {
        strcpy(chans[c].name, names[c]);
        chans[c].pixel_type = EXR_PIXEL_FLOAT;
        chans[c].x_sampling = 1;
        chans[c].y_sampling = 1;
        plane[c] = (float *)malloc(N * sizeof(float));
        CHECK(plane[c]);
        for (i = 0; i < N; ++i) plane[c][i] = pixel(c, i);
    }
    part.header.part_type = EXR_PART_SCANLINE;
    part.header.compression = comp;
    part.header.data_window.max_x = W - 1;
    part.header.data_window.max_y = H - 1;
    part.header.display_window = part.header.data_window;
    part.header.pixel_aspect_ratio = 1.0f;
    part.header.screen_window_width = 1.0f;
    part.header.num_channels = 4;
    part.header.channels = chans;
    part.width = W;
    part.height = H;
    part.images = (void **)plane;
    img.num_parts = 1;
    img.parts = &part;

    CHECK(EXR_OK(exr_save_to_memory(&out, size, NULL, &img, comp)));
    for (c = 0; c < 4; ++c) free(plane[c]);
    return out;
}

/* Loads data and checks every pixel exactly, by channel name since the
 * loader sorts channels. */
static void check_roundtrip(const void *data, size_t size,
                            exr_compression comp) {
    exr_image img;
    const exr_part *p;
    int c, i, k;

    memset(&img, 0, sizeof(img));
    CHECK(EXR_OK(exr_load_from_memory(data, size, NULL, &img)));
    CHECK(img.num_parts == 1);
    p = &img.parts[0];
    CHECK(p->width == W && p->height == H);
    CHECK(p->header.compression == comp);
    CHECK(p->header.num_channels == 4);
    for (c = 0; c < 4; ++c) {
        const float *px = NULL;
        for (k = 0; k < 4; ++k)
            if (strcmp(p->header.channels[k].name, names[c]) == 0) {
                CHECK(p->header.channels[k].pixel_type == EXR_PIXEL_FLOAT);
                px = (const float *)p->images[k];
            }
        CHECK(px);
        for (i = 0; i < N; ++i) CHECK(px[i] == pixel(c, i));
    }
    exr_image_free(&img);
}

int main(void) {
    size_t zip_size, zstd_size;
    unsigned char *zip, *bad;
    void *zstd;
    exr_image img;

    /* ZIP round trip, smaller than the raw pixels, on the libdeflate
     * backend this port builds as the default. */
    CHECK(strcmp(exr_zlib_backend_name(), "libdeflate") == 0);
    zip = (unsigned char *)encode(EXR_COMPRESSION_ZIP, &zip_size);
    CHECK(zip_size < (size_t)N * 4 * sizeof(float));
    check_roundtrip(zip, zip_size, EXR_COMPRESSION_ZIP);

    /* The in-tree codec decodes and encodes the same stream format. */
    CHECK(EXR_OK(exr_zlib_set_backend(EXR_ZLIB_INTREE)));
    CHECK(strcmp(exr_zlib_backend_name(), "in-tree") == 0);
    check_roundtrip(zip, zip_size, EXR_COMPRESSION_ZIP);
    {
        size_t s2;
        void *z2 = encode(EXR_COMPRESSION_ZIP, &s2);
        check_roundtrip(z2, s2, EXR_COMPRESSION_ZIP);
        CHECK(EXR_OK(exr_zlib_set_backend(EXR_ZLIB_AUTO)));
        check_roundtrip(z2, s2, EXR_COMPRESSION_ZIP);
        free(z2);
    }
    CHECK(strcmp(exr_zlib_backend_name(), "libdeflate") == 0);

    /* ZSTD runs the vendored zstd. */
    zstd = encode(EXR_COMPRESSION_ZSTD, &zstd_size);
    check_roundtrip(zstd, zstd_size, EXR_COMPRESSION_ZSTD);
    free(zstd);

    /* A bad magic number, and a truncated stream, are errors. */
    memset(&img, 0, sizeof(img));
    bad = (unsigned char *)malloc(zip_size);
    CHECK(bad);
    memcpy(bad, zip, zip_size);
    bad[0] ^= 0xff;
    CHECK(!EXR_OK(exr_load_from_memory(bad, zip_size, NULL, &img)));
    memcpy(bad, zip, zip_size);
    memset(&img, 0, sizeof(img));
    CHECK(!EXR_OK(exr_load_from_memory(bad, zip_size / 2, NULL, &img)));
    exr_image_free(&img);

    free(bad);
    free(zip);
    puts("ok");
    return 0;
}
