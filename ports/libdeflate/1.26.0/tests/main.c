/* The C consumer: checksums against known vectors and bitwise
 * references, raw DEFLATE, zlib and gzip round trips across the level
 * range, streams written by zlib and Python's gzip module, the
 * decompressor's error results, and the custom allocators. */
#include <libdeflate.h>

#include <stdint.h>
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

/* 1 MiB for the checksums, long enough for the PCLMULQDQ, PMULL and
 * vector Adler-32 paths; a quarter of it for the round trips. */
enum { SAMPLE = 1 << 20, ROUND = SAMPLE / 4 };
static unsigned char in[SAMPLE];
static unsigned char out[ROUND];

/* "Cabin ports libdeflate interop: " + "abc" * 18 + "\n", compressed by
 * Python's zlib.compress(msg, 9) and gzip.compress(msg, compresslevel=9,
 * mtime=0). */
static const char msg[] =
    "Cabin ports libdeflate interop: "
    "abcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabc\n";
static const unsigned char zlib_stream[] = {
    0x78, 0xda, 0x73, 0x4e, 0x4c, 0xca, 0xcc, 0x53, 0x28, 0xc8, 0x2f, 0x2a,
    0x29, 0x56, 0xc8, 0xc9, 0x4c, 0x4a, 0x49, 0x4d, 0xcb, 0x49, 0x2c, 0x49,
    0x55, 0xc8, 0xcc, 0x2b, 0x49, 0x2d, 0xca, 0x2f, 0xb0, 0x52, 0x48, 0x4c,
    0x4a, 0x26, 0x03, 0x71, 0x01, 0x00, 0x9f, 0x1d, 0x20, 0x93};
static const unsigned char gzip_stream[] = {
    0x1f, 0x8b, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0xff, 0x73, 0x4e,
    0x4c, 0xca, 0xcc, 0x53, 0x28, 0xc8, 0x2f, 0x2a, 0x29, 0x56, 0xc8, 0xc9,
    0x4c, 0x4a, 0x49, 0x4d, 0xcb, 0x49, 0x2c, 0x49, 0x55, 0xc8, 0xcc, 0x2b,
    0x49, 0x2d, 0xca, 0x2f, 0xb0, 0x52, 0x48, 0x4c, 0x4a, 0x26, 0x03, 0x71,
    0x01, 0x00, 0xf5, 0x57, 0x97, 0x28, 0x57, 0x00, 0x00, 0x00};

/* Words drawn by an LCG with a stray random byte now and then: text
 * that compresses well but not trivially. */
static void fill(unsigned char *p, size_t n) {
    static const char *const words[] = {"cabin ", "port ", "deflate ",
                                        "zlib ",  "gzip ", "crc ",
                                        "adler ", "huffman ", "\n"};
    uint32_t x = 12345;
    size_t i = 0;
    while (i < n) {
        x = x * 1103515245u + 12345u;
        for (const char *w = words[(x >> 16) % 9]; *w && i < n; w++)
            p[i++] = (unsigned char)*w;
        if ((x >> 8) % 16 == 0 && i < n) p[i++] = (unsigned char)(x >> 24);
    }
}

static uint32_t crc32_ref(const unsigned char *p, size_t n) {
    uint32_t crc = 0xffffffffu;
    for (size_t i = 0; i < n; i++) {
        crc ^= p[i];
        for (int k = 0; k < 8; k++)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1)));
    }
    return ~crc;
}

static uint32_t adler32_ref(const unsigned char *p, size_t n) {
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < n; i++) {
        a = (a + p[i]) % 65521;
        b = (b + a) % 65521;
    }
    return b << 16 | a;
}

typedef size_t (*compress_fn)(struct libdeflate_compressor *, const void *,
                              size_t, void *, size_t);
typedef size_t (*bound_fn)(struct libdeflate_compressor *, size_t);
typedef enum libdeflate_result (*decompress_fn)(
    struct libdeflate_decompressor *, const void *, size_t, void *, size_t,
    size_t *);

static const struct format {
    compress_fn compress;
    bound_fn bound;
    decompress_fn decompress;
} formats[] = {
    {libdeflate_deflate_compress, libdeflate_deflate_compress_bound,
     libdeflate_deflate_decompress},
    {libdeflate_zlib_compress, libdeflate_zlib_compress_bound,
     libdeflate_zlib_decompress},
    {libdeflate_gzip_compress, libdeflate_gzip_compress_bound,
     libdeflate_gzip_decompress},
};

static size_t mallocs, frees;
static void *counting_malloc(size_t n) {
    mallocs++;
    return malloc(n);
}
static void counting_free(void *p) {
    frees++;
    free(p);
}

static void checksums(void) {
    CHECK(libdeflate_crc32(0, "123456789", 9) == 0xcbf43926u);
    CHECK(libdeflate_adler32(1, "Wikipedia", 9) == 0x11e60398u);
    /* What zlib computed for the interop message. */
    CHECK(libdeflate_crc32(0, msg, sizeof msg - 1) == 0x289757f5u);
    CHECK(libdeflate_adler32(1, msg, sizeof msg - 1) == 0x9f1d2093u);

    const uint32_t crc = crc32_ref(in, SAMPLE);
    const uint32_t adler = adler32_ref(in, SAMPLE);
    CHECK(libdeflate_crc32(0, in, SAMPLE) == crc);
    CHECK(libdeflate_adler32(1, in, SAMPLE) == adler);
    /* Chained over an unaligned split. */
    const size_t k = 4099;
    CHECK(libdeflate_crc32(libdeflate_crc32(0, in, k), in + k, SAMPLE - k) ==
          crc);
    CHECK(libdeflate_adler32(libdeflate_adler32(1, in, k), in + k,
                             SAMPLE - k) == adler);
}

static void round_trips(void) {
    static const int levels[] = {0, 1, 6, 9, 12};
    const size_t nlevels = sizeof levels / sizeof levels[0];
    size_t sizes[3][5];
    const size_t cap = libdeflate_gzip_compress_bound(NULL, ROUND);
    unsigned char *packed = malloc(cap);
    struct libdeflate_decompressor *d = libdeflate_alloc_decompressor();
    CHECK(packed && d);

    for (size_t l = 0; l < nlevels; l++) {
        struct libdeflate_compressor *c = libdeflate_alloc_compressor(levels[l]);
        CHECK(c);
        for (size_t f = 0; f < 3; f++) {
            const struct format *fmt = &formats[f];
            const size_t bound = fmt->bound(c, ROUND);
            CHECK(bound >= ROUND && bound <= fmt->bound(NULL, ROUND) &&
                  bound <= cap);
            const size_t n = fmt->compress(c, in, ROUND, packed, bound);
            CHECK(n > 0 && n <= bound);
            CHECK(levels[l] == 0 ? n > ROUND : n < ROUND / 2);
            sizes[f][l] = n;
            memset(out, 0, ROUND);
            CHECK(fmt->decompress(d, packed, n, out, ROUND, NULL) ==
                  LIBDEFLATE_SUCCESS);
            CHECK(memcmp(out, in, ROUND) == 0);
            /* Too little room to compress into. */
            CHECK(fmt->compress(c, in, ROUND, packed, 64) == 0);
        }
        libdeflate_free_compressor(c);
    }
    for (size_t f = 0; f < 3; f++) CHECK(sizes[f][4] < sizes[f][1]);

    /* -1 is level 6. */
    struct libdeflate_compressor *c6 = libdeflate_alloc_compressor(-1);
    CHECK(c6);
    CHECK(libdeflate_zlib_compress(c6, in, ROUND, packed, cap) == sizes[1][2]);
    libdeflate_free_compressor(c6);
    CHECK(libdeflate_alloc_compressor(13) == NULL);
    CHECK(libdeflate_alloc_compressor(-2) == NULL);

    libdeflate_free_decompressor(d);
    free(packed);
}

static void interop_and_errors(void) {
    struct libdeflate_decompressor *d = libdeflate_alloc_decompressor();
    CHECK(d);
    const size_t len = sizeof msg - 1;
    char buf[128];
    size_t got = 0, used = 0;

    CHECK(libdeflate_zlib_decompress(d, zlib_stream, sizeof zlib_stream, buf,
                                     sizeof buf, &got) == LIBDEFLATE_SUCCESS);
    CHECK(got == len && memcmp(buf, msg, len) == 0);
    /* The raw DEFLATE stream inside the zlib wrapper. */
    CHECK(libdeflate_deflate_decompress_ex(d, zlib_stream + 2,
                                           sizeof zlib_stream - 2, buf, len,
                                           &used, NULL) == LIBDEFLATE_SUCCESS);
    CHECK(used == sizeof zlib_stream - 6 && memcmp(buf, msg, len) == 0);

    /* A gzip member followed by unrelated bytes. */
    unsigned char trailing[sizeof gzip_stream + 5];
    memcpy(trailing, gzip_stream, sizeof gzip_stream);
    memcpy(trailing + sizeof gzip_stream, "extra", 5);
    CHECK(libdeflate_gzip_decompress_ex(d, trailing, sizeof trailing, buf,
                                        sizeof buf, &used,
                                        &got) == LIBDEFLATE_SUCCESS);
    CHECK(used == sizeof gzip_stream && got == len &&
          memcmp(buf, msg, len) == 0);

    /* Output size results. */
    CHECK(libdeflate_zlib_decompress(d, zlib_stream, sizeof zlib_stream, buf,
                                     len, NULL) == LIBDEFLATE_SUCCESS);
    CHECK(libdeflate_zlib_decompress(d, zlib_stream, sizeof zlib_stream, buf,
                                     len + 1, NULL) ==
          LIBDEFLATE_SHORT_OUTPUT);
    CHECK(libdeflate_zlib_decompress(d, zlib_stream, sizeof zlib_stream, buf,
                                     len - 1, &got) ==
          LIBDEFLATE_INSUFFICIENT_SPACE);

    /* Damaged streams. */
    unsigned char bad[sizeof gzip_stream];
    memcpy(bad, zlib_stream, sizeof zlib_stream);
    bad[sizeof zlib_stream - 1] ^= 1; /* Adler-32 */
    CHECK(libdeflate_zlib_decompress(d, bad, sizeof zlib_stream, buf,
                                     sizeof buf, &got) == LIBDEFLATE_BAD_DATA);
    CHECK(libdeflate_zlib_decompress(d, zlib_stream, sizeof zlib_stream - 10,
                                     buf, sizeof buf,
                                     &got) == LIBDEFLATE_BAD_DATA);
    memcpy(bad, gzip_stream, sizeof gzip_stream);
    bad[sizeof gzip_stream - 8] ^= 1; /* CRC-32 */
    CHECK(libdeflate_gzip_decompress(d, bad, sizeof bad, buf, sizeof buf,
                                     &got) == LIBDEFLATE_BAD_DATA);
    memcpy(bad, gzip_stream, sizeof gzip_stream);
    bad[sizeof gzip_stream - 4] ^= 1; /* ISIZE */
    CHECK(libdeflate_gzip_decompress(d, bad, sizeof bad, buf, sizeof buf,
                                     &got) == LIBDEFLATE_BAD_DATA);
    memcpy(bad, gzip_stream, sizeof gzip_stream);
    bad[0] = 0; /* magic */
    CHECK(libdeflate_gzip_decompress(d, bad, sizeof bad, buf, sizeof buf,
                                     &got) == LIBDEFLATE_BAD_DATA);

    libdeflate_free_decompressor(d);
}

static void allocators(void) {
    struct libdeflate_options options;
    memset(&options, 0, sizeof options);
    options.sizeof_options = sizeof options;
    options.malloc_func = counting_malloc;
    options.free_func = counting_free;

    struct libdeflate_compressor *c = libdeflate_alloc_compressor_ex(9, &options);
    struct libdeflate_decompressor *d = libdeflate_alloc_decompressor_ex(&options);
    CHECK(c && d && mallocs == 2 && frees == 0);
    unsigned char packed[256];
    const size_t n =
        libdeflate_gzip_compress(c, msg, sizeof msg - 1, packed, sizeof packed);
    char buf[128];
    CHECK(n > 0 && libdeflate_gzip_decompress(d, packed, n, buf, sizeof msg - 1,
                                              NULL) == LIBDEFLATE_SUCCESS);
    CHECK(memcmp(buf, msg, sizeof msg - 1) == 0);
    libdeflate_free_compressor(c);
    libdeflate_free_decompressor(d);
    CHECK(mallocs == 2 && frees == 2);

    options.sizeof_options = sizeof options - 1;
    CHECK(libdeflate_alloc_decompressor_ex(&options) == NULL);

    /* The process-wide allocator. */
    libdeflate_set_memory_allocator(counting_malloc, counting_free);
    d = libdeflate_alloc_decompressor();
    CHECK(d && mallocs == 3);
    libdeflate_free_decompressor(d);
    CHECK(frees == 3);
    libdeflate_set_memory_allocator(malloc, free);
}

int main(void) {
    CHECK(LIBDEFLATE_VERSION_MAJOR == 1 && LIBDEFLATE_VERSION_MINOR == 26);
    CHECK(strcmp(LIBDEFLATE_VERSION_STRING, "1.26") == 0);
    fill(in, SAMPLE);
    checksums();
    round_trips();
    interop_and_errors();
    allocators();
    puts("ok");
    return 0;
}
