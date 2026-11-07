/* A PNG built by hand from the specification decodes to its pixels; images
 * encoded with one filter type per run come back byte for byte, every row
 * with that filter, which drives the SSE2 and NEON defilters for 3- and
 * 4-byte pixels; palette, transparency, gamma, 16-bit, text and error
 * paths.  C99. */
#include <spng.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

static void check(int ok, const char *what)
{
    if (!ok) {
        fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

/* 9x5 RGB8, rows filtered None, Sub, Up, Average, Paeth by an independent
 * encoder (a short Python script: the PNG filter definitions, zlib, CRC-32),
 * pixel (x, y) = (x*29 + y*71 + 13, x*x*7 + y*3, 255 - x*23 - y*41) mod 256. */
static const unsigned char reference_png[] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d,
    0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0x05,
    0x08, 0x02, 0x00, 0x00, 0x00, 0x18, 0x31, 0x51, 0x3c, 0x00, 0x00, 0x00,
    0x74, 0x49, 0x44, 0x41, 0x54, 0x78, 0xda, 0x63, 0xe0, 0x65, 0xf8, 0xaf,
    0xc5, 0xfe, 0xc2, 0x5d, 0xe6, 0x62, 0x8a, 0xfd, 0xae, 0xc6, 0x82, 0xc5,
    0xf3, 0xd6, 0xf7, 0xec, 0xfe, 0x53, 0x7a, 0x23, 0x3c, 0xee, 0xeb, 0x01,
    0x77, 0xc6, 0x10, 0xe6, 0x6b, 0xb2, 0xec, 0x2f, 0x65, 0x45, 0x5f, 0xca,
    0x2a, 0xbf, 0x94, 0x35, 0x7c, 0x29, 0x6b, 0xff, 0x52, 0xd6, 0xf7, 0xa5,
    0x6c, 0xf4, 0x4b, 0xd9, 0xcc, 0x97, 0x4c, 0xee, 0xcc, 0xd7, 0x71, 0x21,
    0xe6, 0xa9, 0x6c, 0x7a, 0x46, 0xac, 0x0f, 0x8c, 0x78, 0x1e, 0x6c, 0x12,
    0x7e, 0x60, 0x24, 0xf5, 0xc0, 0x48, 0xf1, 0x81, 0xd1, 0x8a, 0x07, 0x46,
    0xfa, 0x09, 0x46, 0x66, 0x0f, 0x58, 0x80, 0xf2, 0xb2, 0xcc, 0x2f, 0xdd,
    0x99, 0x5f, 0xca, 0x22, 0xd0, 0x75, 0x08, 0x03, 0x00, 0xec, 0xbc, 0x37,
    0x6f, 0x72, 0x2d, 0x87, 0x03, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e,
    0x44, 0xae, 0x42, 0x60, 0x82,
};

static void reference(void)
{
    spng_ctx *ctx = spng_ctx_new(0);
    struct spng_ihdr ihdr;
    unsigned char out[5][9][4];
    size_t size = 0;
    int ok = 1;
    int x, y;

    spng_set_png_buffer(ctx, reference_png, sizeof reference_png);
    check(spng_get_ihdr(ctx, &ihdr) == 0 && ihdr.width == 9 && ihdr.height == 5 &&
              ihdr.bit_depth == 8 && ihdr.color_type == SPNG_COLOR_TYPE_TRUECOLOR,
          "reference IHDR");
    check(spng_decoded_image_size(ctx, SPNG_FMT_RGBA8, &size) == 0 && size == sizeof out,
          "reference RGBA8 size");
    check(spng_decode_image(ctx, out, sizeof out, SPNG_FMT_RGBA8, 0) == 0,
          "reference decode");
    for (y = 0; y < 5; ++y)
        for (x = 0; x < 9; ++x) {
            const unsigned char *p = out[y][x];
            ok = ok && p[0] == ((x * 29 + y * 71 + 13) & 255) &&
                 p[1] == ((x * x * 7 + y * 3) & 255) &&
                 p[2] == ((255 - x * 23 - y * 41) & 255) && p[3] == 255;
        }
    check(ok, "reference pixels");
    spng_ctx_free(ctx);
}

/* Encodes with SPNG_FMT_PNG; the caller frees the buffer. */
static unsigned char *encode(struct spng_ihdr *ihdr, const void *image, size_t len,
                             int filter_choice, size_t *png_len)
{
    spng_ctx *enc = spng_ctx_new(SPNG_CTX_ENCODER);
    unsigned char *png;
    int err = 0;

    spng_set_option(enc, SPNG_ENCODE_TO_BUFFER, 1);
    spng_set_option(enc, SPNG_FILTER_CHOICE, filter_choice);
    spng_set_ihdr(enc, ihdr);
    check(spng_encode_image(enc, image, len, SPNG_FMT_PNG, SPNG_ENCODE_FINALIZE) == 0,
          "encode");
    png = spng_get_png_buffer(enc, png_len, &err);
    check(png != NULL && err == 0, "encoded buffer");
    spng_ctx_free(enc);
    return png;
}

enum { W = 37, H = 6 };

static void filters(void)
{
    static const struct { int choice; uint8_t filter; } runs[] = {
        {SPNG_FILTER_CHOICE_SUB, SPNG_FILTER_SUB},
        {SPNG_FILTER_CHOICE_UP, SPNG_FILTER_UP},
        {SPNG_FILTER_CHOICE_AVG, SPNG_FILTER_AVERAGE},
        {SPNG_FILTER_CHOICE_PAETH, SPNG_FILTER_PAETH},
    };
    unsigned char image[H * W * 4], out[H * W * 4];
    uint32_t seed = 12345;
    size_t i;
    int channels, r;

    for (i = 0; i < sizeof image; ++i) {
        seed = seed * 1103515245u + 12345u;
        image[i] = (unsigned char)(seed >> 16);
    }

    for (channels = 3; channels <= 4; ++channels) {
        const size_t len = (size_t)H * W * channels;
        for (r = 0; r < 4; ++r) {
            struct spng_ihdr ihdr = {W, H, 8, 0, 0, 0, 0};
            struct spng_row_info info;
            spng_ctx *dec = spng_ctx_new(0);
            unsigned char *png;
            size_t png_len = 0;
            int rows = 0, all_filtered = 1, err;

            ihdr.color_type = channels == 3 ? SPNG_COLOR_TYPE_TRUECOLOR
                                            : SPNG_COLOR_TYPE_TRUECOLOR_ALPHA;
            png = encode(&ihdr, image, len, runs[r].choice, &png_len);
            spng_set_png_buffer(dec, png, png_len);
            memset(out, 0, sizeof out);
            err = spng_decode_image(dec, NULL, 0, SPNG_FMT_PNG, SPNG_DECODE_PROGRESSIVE);
            check(err == 0, "progressive decode");
            do {
                err = spng_get_row_info(dec, &info);
                if (err) break;
                all_filtered = all_filtered && info.filter == runs[r].filter;
                err = spng_decode_row(dec, out + (size_t)info.row_num * W * channels,
                                      (size_t)W * channels);
                ++rows;
            } while (!err);
            check(err == SPNG_EOI && rows == H, "every row decoded");
            check(all_filtered, "every row carries the chosen filter");
            check(memcmp(image, out, len) == 0, "filtered round trip");
            spng_ctx_free(dec);
            free(png);
        }
    }
}

static void conversions(void)
{
    /* Palette with tRNS: index 1 is half transparent. */
    spng_ctx *enc = spng_ctx_new(SPNG_CTX_ENCODER);
    spng_ctx *dec;
    struct spng_ihdr ihdr = {4, 1, 8, SPNG_COLOR_TYPE_INDEXED, 0, 0, 0};
    struct spng_plte plte;
    struct spng_trns trns;
    const unsigned char indices[4] = {0, 1, 2, 1};
    unsigned char rgba[4][4];
    unsigned char *png;
    size_t png_len = 0;
    int err = 0;

    memset(&plte, 0, sizeof plte);
    memset(&trns, 0, sizeof trns);
    plte.n_entries = 3;
    plte.entries[0].red = 255;
    plte.entries[1].green = 200;
    plte.entries[2].blue = 100;
    trns.n_type3_entries = 2;
    trns.type3_alpha[0] = 255;
    trns.type3_alpha[1] = 128;
    spng_set_option(enc, SPNG_ENCODE_TO_BUFFER, 1);
    spng_set_ihdr(enc, &ihdr);
    spng_set_plte(enc, &plte);
    spng_set_trns(enc, &trns);
    check(spng_encode_image(enc, indices, sizeof indices, SPNG_FMT_PNG,
                            SPNG_ENCODE_FINALIZE) == 0,
          "encode indexed");
    png = spng_get_png_buffer(enc, &png_len, &err);
    spng_ctx_free(enc);

    dec = spng_ctx_new(0);
    spng_set_png_buffer(dec, png, png_len);
    check(spng_decode_image(dec, rgba, sizeof rgba, SPNG_FMT_RGBA8, SPNG_DECODE_TRNS) == 0,
          "decode indexed");
    check(memcmp(rgba[0], "\xff\x00\x00\xff", 4) == 0 &&
              memcmp(rgba[1], "\x00\xc8\x00\x80", 4) == 0 &&
              memcmp(rgba[2], "\x00\x00\x64\xff", 4) == 0 &&
              memcmp(rgba[3], rgba[1], 4) == 0,
          "palette and tRNS expansion");
    spng_ctx_free(dec);
    free(png);
}

static void gamma_and_text(void)
{
    /* gAMA 1.0 against the default 2.2 screen: v -> 255 * (v/255)^(1/2.2),
     * truncated; 64 -> 136.03, 128 -> 186.42.  pow() comes from libm. */
    spng_ctx *enc = spng_ctx_new(SPNG_CTX_ENCODER);
    spng_ctx *dec;
    struct spng_ihdr ihdr = {4, 1, 8, SPNG_COLOR_TYPE_GRAYSCALE, 0, 0, 0};
    struct spng_text text;
    struct spng_text got[2];
    uint32_t n_text = 0;
    const unsigned char gray[4] = {0, 64, 128, 255};
    unsigned char rgba[4][4];
    unsigned char *png;
    size_t png_len = 0;
    double gamma = 0;
    int err = 0;
    char comment[] = "made by cabin";

    memset(&text, 0, sizeof text);
    memcpy(text.keyword, "Comment", sizeof "Comment");
    text.type = SPNG_TEXT;
    text.length = strlen(comment);
    text.text = comment;
    spng_set_option(enc, SPNG_ENCODE_TO_BUFFER, 1);
    spng_set_ihdr(enc, &ihdr);
    spng_set_gama(enc, 1.0);
    spng_set_text(enc, &text, 1);
    check(spng_encode_image(enc, gray, sizeof gray, SPNG_FMT_PNG, SPNG_ENCODE_FINALIZE) == 0,
          "encode gray");
    png = spng_get_png_buffer(enc, &png_len, &err);
    spng_ctx_free(enc);

    dec = spng_ctx_new(0);
    spng_set_png_buffer(dec, png, png_len);
    check(spng_decode_image(dec, rgba, sizeof rgba, SPNG_FMT_RGBA8, SPNG_DECODE_GAMMA) == 0,
          "decode with gamma");
    check(rgba[0][0] == 0 && rgba[1][0] == 136 && rgba[2][0] == 186 && rgba[3][0] == 255 &&
              rgba[1][1] == 136 && rgba[1][2] == 136 && rgba[1][3] == 255,
          "gamma correction");
    check(spng_get_gama(dec, &gamma) == 0 && gamma == 1.0, "gAMA");
    /* A NULL array asks for the count; the fill leaves *n_text alone. */
    check(spng_get_text(dec, NULL, &n_text) == 0 && n_text == 1 &&
              spng_get_text(dec, got, &n_text) == 0 &&
              strcmp(got[0].keyword, "Comment") == 0 && got[0].type == SPNG_TEXT &&
              got[0].length == strlen(comment) && strcmp(got[0].text, comment) == 0,
          "tEXt");
    spng_ctx_free(dec);
    free(png);
}

static void sixteen_bit(void)
{
    /* SPNG_FMT_PNG is host-endian, SPNG_FMT_RAW the file's big-endian. */
    struct spng_ihdr ihdr = {2, 1, 16, SPNG_COLOR_TYPE_GRAYSCALE, 0, 0, 0};
    const uint16_t samples[2] = {0x1234, 0xfedc};
    uint16_t host[2];
    unsigned char raw[4];
    size_t png_len = 0;
    unsigned char *png = encode(&ihdr, samples, sizeof samples, SPNG_FILTER_CHOICE_ALL,
                                &png_len);
    spng_ctx *dec = spng_ctx_new(0);

    spng_set_png_buffer(dec, png, png_len);
    check(spng_decode_image(dec, host, sizeof host, SPNG_FMT_PNG, 0) == 0 &&
              host[0] == 0x1234 && host[1] == 0xfedc,
          "16-bit host order");
    spng_ctx_free(dec);
    dec = spng_ctx_new(0);
    spng_set_png_buffer(dec, png, png_len);
    check(spng_decode_image(dec, raw, sizeof raw, SPNG_FMT_RAW, 0) == 0 &&
              memcmp(raw, "\x12\x34\xfe\xdc", 4) == 0,
          "16-bit big-endian");
    spng_ctx_free(dec);
    free(png);
}

static void errors(void)
{
    unsigned char png[sizeof reference_png];
    struct spng_ihdr ihdr;
    unsigned char out[5 * 9 * 3];
    spng_ctx *ctx;

    memcpy(png, reference_png, sizeof png);
    png[0] = 0x88;
    ctx = spng_ctx_new(0);
    spng_set_png_buffer(ctx, png, sizeof png);
    check(spng_get_ihdr(ctx, &ihdr) == SPNG_ESIGNATURE, "bad signature");
    spng_ctx_free(ctx);

    /* IHDR's CRC is checked once the next chunk is read, so spng_get_ihdr
     * still succeeds; the decode fails unless the CRC is set to be ignored. */
    memcpy(png, reference_png, sizeof png);
    png[29] ^= 1;
    ctx = spng_ctx_new(0);
    spng_set_png_buffer(ctx, png, sizeof png);
    check(spng_get_ihdr(ctx, &ihdr) == 0 &&
              spng_decode_image(ctx, out, sizeof out, SPNG_FMT_PNG, 0) == SPNG_ECHUNK_CRC,
          "IHDR CRC mismatch");
    spng_ctx_free(ctx);
    ctx = spng_ctx_new(0);
    spng_set_crc_action(ctx, SPNG_CRC_USE, SPNG_CRC_USE);
    spng_set_png_buffer(ctx, png, sizeof png);
    check(spng_decode_image(ctx, out, sizeof out, SPNG_FMT_PNG, 0) == 0 && out[0] == 13 &&
              out[sizeof out - 1] == 163,
          "CRC mismatch ignored on request");
    spng_ctx_free(ctx);

    ctx = spng_ctx_new(0);
    spng_set_png_buffer(ctx, reference_png, 60); /* cut inside IDAT */
    check(spng_decode_image(ctx, out, sizeof out, SPNG_FMT_PNG, 0) == SPNG_IO_EOF,
          "truncated PNG");
    spng_ctx_free(ctx);

    ctx = spng_ctx_new(0);
    spng_set_png_buffer(ctx, reference_png, sizeof reference_png);
    check(spng_decode_image(ctx, out, sizeof out - 1, SPNG_FMT_PNG, 0) == SPNG_EBUFSIZ,
          "short output buffer");
    spng_ctx_free(ctx);

    check(strcmp(spng_strerror(SPNG_OK), "success") == 0 &&
              strcmp(spng_strerror(SPNG_ECHUNK_CRC), spng_strerror(SPNG_OK)) != 0,
          "spng_strerror");
}

int main(void)
{
    char version[16];

    snprintf(version, sizeof version, "%d.%d.%d", SPNG_VERSION_MAJOR, SPNG_VERSION_MINOR,
             SPNG_VERSION_PATCH);
    check(strcmp(version, "0.7.4") == 0 && strcmp(spng_version_string(), version) == 0,
          "version");
    reference();
    filters();
    conversions();
    gamma_and_text();
    sixteen_bit();
    errors();
    if (failures != 0) return 1;
    puts("libspng consumer ok");
    return 0;
}
