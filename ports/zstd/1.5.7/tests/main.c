/* The C99 consumer: decodes hand-built current and v0.5 frames,
 * round-trips data with worker threads, rejects a frame whose checksum
 * was damaged, trains and uses a dictionary, and checks the renamed
 * bundled xxHash. */
#include <zdict.h>
#include <zstd.h>
#include <zstd_errors.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* The bundled xxHash is compiled with XXH_NAMESPACE=ZSTD_. */
uint64_t ZSTD_XXH64(const void* input, size_t length, uint64_t seed);

/* Above ZSTDMT_JOBSIZE_MIN (512 KiB): zstd compresses smaller inputs on
 * the calling thread whatever ZSTD_c_nbWorkers says. */
enum { SAMPLE = 1 << 20 };
static char in[SAMPLE];
static char packed[SAMPLE + 1024];
static char out[SAMPLE];

static int fail(const char* what) {
    fprintf(stderr, "zstd C test failed: %s\n", what);
    return 0;
}

static int decodes_a_known_frame(void) {
    /* Magic, a single-segment header with a 1-byte content size of 5,
     * then one last raw block of 5 bytes (header 5 << 3 | last). */
    const unsigned char frame[] = {0x28, 0xB5, 0x2F, 0xFD, 0x20, 0x05, 0x29, 0x00,
                                   0x00, 'h',  'e',  'l',  'l',  'o'};
    char buf[5];
    if (ZSTD_getFrameContentSize(frame, sizeof frame) != 5) return fail("frame content size");
    const size_t n = ZSTD_decompress(buf, sizeof buf, frame, sizeof frame);
    return (!ZSTD_isError(n) && n == 5 && memcmp(buf, "hello", 5) == 0) || fail("known frame");
}

static int decodes_a_legacy_frame(void) {
    /* A v0.5 frame, readable because ZSTD_LEGACY_SUPPORT=5 keeps the
     * v0.5-v0.7 decoders: magic, a header byte selecting the smallest
     * window, a raw block of 5 bytes (type 1 << 6), then the end block
     * (type 3 << 6). */
    const unsigned char frame[] = {0x25, 0xB5, 0x2F, 0xFD, 0x00, 0x40, 0x00, 0x05,
                                   'h',  'e',  'l',  'l',  'o',  0xC0, 0x00, 0x00};
    char buf[5];
    const size_t n = ZSTD_decompress(buf, sizeof buf, frame, sizeof frame);
    return (!ZSTD_isError(n) && n == 5 && memcmp(buf, "hello", 5) == 0) || fail("legacy frame");
}

static int round_trips(ZSTD_CCtx* cctx) {
    const size_t n = ZSTD_compress2(cctx, packed, sizeof packed, in, SAMPLE);
    if (ZSTD_isError(n) || n >= SAMPLE / 8) return fail("multithreaded compression");
    const size_t m = ZSTD_decompress(out, SAMPLE, packed, n);
    return (!ZSTD_isError(m) && m == SAMPLE && memcmp(out, in, SAMPLE) == 0) ||
           fail("multithreaded round trip");
}

static int compresses_with_workers_and_checksum(void) {
    ZSTD_CCtx* cctx = ZSTD_createCCtx();
    /* Setting workers fails with parameter_unsupported unless the
     * library was built with ZSTD_MULTITHREAD. */
    int ok = (!ZSTD_isError(ZSTD_CCtx_setParameter(cctx, ZSTD_c_nbWorkers, 2)) ||
              fail("multithreading unavailable")) &&
             !ZSTD_isError(ZSTD_CCtx_setParameter(cctx, ZSTD_c_checksumFlag, 1)) &&
             round_trips(cctx);
    if (ok) {
        const size_t n = ZSTD_compress2(cctx, packed, sizeof packed, in, SAMPLE);
        if (ZSTD_isError(n)) {
            ZSTD_freeCCtx(cctx);
            return fail("checksummed compression");
        }
        packed[n - 1] ^= 0x01; /* the last byte belongs to the content checksum */
        const size_t m = ZSTD_decompress(out, SAMPLE, packed, n);
        ok = (ZSTD_isError(m) && ZSTD_getErrorCode(m) == ZSTD_error_checksum_wrong) ||
             fail("damaged checksum accepted");
    }
    ZSTD_freeCCtx(cctx);
    return ok;
}

static int trains_and_uses_a_dictionary(void) {
    /* 300 small records sharing structure: the dictionary builder's
     * use case. */
    enum { RECORDS = 300 };
    static char samples[RECORDS * 64];
    static size_t sizes[RECORDS];
    static char dict[2048];
    size_t total = 0;
    for (int i = 0; i < RECORDS; ++i) {
        const int n = snprintf(samples + total, 64, "{\"name\":\"port-%d\",\"version\":\"1.%d.%d\"}\n",
                               i, i % 17, i % 5);
        sizes[i] = (size_t)n;
        total += (size_t)n;
    }
    const size_t dict_size = ZDICT_trainFromBuffer(dict, sizeof dict, samples, sizes, RECORDS);
    if (ZDICT_isError(dict_size)) return fail(ZDICT_getErrorName(dict_size));
    const unsigned id = ZDICT_getDictID(dict, dict_size);
    if (id == 0) return fail("trained dictionary has no ID");

    ZSTD_CCtx* cctx = ZSTD_createCCtx();
    const size_t n = ZSTD_compress_usingDict(cctx, packed, sizeof packed, samples, sizes[0], dict,
                                             dict_size, 3);
    ZSTD_freeCCtx(cctx);
    if (ZSTD_isError(n) || ZSTD_getDictID_fromFrame(packed, n) != id) return fail("dictionary frame");
    ZSTD_DCtx* dctx = ZSTD_createDCtx();
    const size_t m = ZSTD_decompress_usingDict(dctx, out, SAMPLE, packed, n, dict, dict_size);
    const size_t without = ZSTD_decompressDCtx(dctx, out + sizes[0], SAMPLE - sizes[0], packed, n);
    ZSTD_freeDCtx(dctx);
    return ((!ZSTD_isError(m) && m == sizes[0] && memcmp(out, samples, m) == 0) ||
            fail("dictionary round trip")) &&
           ((ZSTD_isError(without) &&
             ZSTD_getErrorCode(without) == ZSTD_error_dictionary_wrong) ||
            fail("frame decoded without its dictionary"));
}

int main(void) {
    for (int i = 0; i < SAMPLE; ++i) in[i] = "Cabin consumes cabin-ports/zstd from C99\n"[i % 41];
    const int ok = (ZSTD_versionNumber() == ZSTD_VERSION_NUMBER || fail("version")) &&
                   (ZSTD_XXH64("", 0, 0) == UINT64_C(0xEF46DB3751D8E999) || fail("bundled xxHash")) &&
                   decodes_a_known_frame() && decodes_a_legacy_frame() &&
                   compresses_with_workers_and_checksum() &&
                   trains_and_uses_a_dictionary();
    return ok ? 0 : 1;
}
