/* The C99 consumer: decodes a fixed LZ4 block, round-trips data through
 * the block, HC, and checksummed frame APIs, and checks that corrupt
 * input is rejected. */
#include <lz4.h>
#include <lz4file.h>
#include <lz4frame.h>
#include <lz4hc.h>

#include <stdio.h>
#include <string.h>

/* The bundled xxHash is compiled with XXH_NAMESPACE=LZ4_. */
unsigned LZ4_XXH_versionNumber(void);

enum { SAMPLE = 16000 };
static char in[SAMPLE];
static char packed[LZ4_COMPRESSBOUND(SAMPLE)];
static char out[SAMPLE];
static unsigned char frame[SAMPLE + 1024];

static int fail(const char* what) {
    fprintf(stderr, "lz4 C test failed: %s\n", what);
    return 0;
}

static int decodes_a_known_block(void) {
    const unsigned char block[] = {0x56, 'a', 'b', 'c', 'd', 'e', 0x05, 0x00,
                                   0x50, 'v', 'w', 'x', 'y', 'z'};
    const char expected[] = "abcdeabcdeabcdevwxyz";
    char buf[sizeof expected - 1];
    const int n = LZ4_decompress_safe((const char*)block, buf, (int)sizeof block, (int)sizeof buf);
    return (n == (int)sizeof buf && memcmp(buf, expected, sizeof buf) == 0) || fail("known block");
}

static int round_trips_blocks(void) {
    const int fast = LZ4_compress_default(in, packed, SAMPLE, (int)sizeof packed);
    if (fast <= 0 || fast >= SAMPLE / 4) return fail("compress_default");
    if (LZ4_decompress_safe(packed, out, fast, SAMPLE) != SAMPLE || memcmp(out, in, SAMPLE) != 0)
        return fail("block round trip");
    const int hc = LZ4_compress_HC(in, packed, SAMPLE, (int)sizeof packed, LZ4HC_CLEVEL_MAX);
    if (hc <= 0 || hc > fast) return fail("compress_HC");
    if (LZ4_decompress_safe(packed, out, hc, SAMPLE) != SAMPLE || memcmp(out, in, SAMPLE) != 0)
        return fail("HC round trip");
    packed[0] = (char)0xF0;
    return LZ4_decompress_safe(packed, out, 3, SAMPLE) < 0 || fail("corrupt block accepted");
}

static int round_trips_a_checksummed_frame(void) {
    LZ4F_preferences_t prefs;
    memset(&prefs, 0, sizeof prefs);
    prefs.frameInfo.contentChecksumFlag = LZ4F_contentChecksumEnabled;
    if (LZ4F_compressFrameBound(SAMPLE, &prefs) > sizeof frame) return fail("frame bound");
    const size_t n = LZ4F_compressFrame(frame, sizeof frame, in, SAMPLE, &prefs);
    if (LZ4F_isError(n)) return fail(LZ4F_getErrorName(n));

    LZ4F_dctx* dctx = NULL;
    if (LZ4F_isError(LZ4F_createDecompressionContext(&dctx, LZ4F_VERSION))) return fail("dctx");
    size_t src = n;
    size_t dst = SAMPLE;
    size_t left = LZ4F_decompress(dctx, out, &dst, frame, &src, NULL);
    int ok = (!LZ4F_isError(left) && left == 0 && dst == SAMPLE && memcmp(out, in, SAMPLE) == 0) ||
             fail("frame round trip");

    frame[n - 1] ^= 0x01;
    LZ4F_resetDecompressionContext(dctx);
    src = n;
    dst = SAMPLE;
    left = LZ4F_decompress(dctx, out, &dst, frame, &src, NULL);
    ok = ok && (LZ4F_isError(left) || fail("bad checksum accepted"));
    LZ4F_freeDecompressionContext(dctx);
    return ok;
}

int main(void) {
    for (int i = 0; i < SAMPLE; ++i) in[i] = "Cabin consumes cabin-ports/lz4 from C99\n"[i % 40];
    const int ok = (LZ4_versionNumber() == LZ4_VERSION_NUMBER || fail("version")) &&
                   (LZ4_XXH_versionNumber() / 100 == 6 || fail("bundled xxHash")) &&
                   decodes_a_known_block() && round_trips_blocks() &&
                   round_trips_a_checksummed_frame();
    return ok ? 0 : 1;
}
