/* The C89 consumer: compresses a known input to the exact stream bzip2
 * 1.0.8 produces, round-trips a larger buffer in normal and small
 * decompression modes, and checks that damaged and truncated streams
 * are rejected. */
#include <bzlib.h>

#include <stdio.h>
#include <string.h>

/* `printf hello | bzip2 -9`, from bzip2 1.0.8. */
static const unsigned char hello_bz2[] = {
    0x42, 0x5a, 0x68, 0x39, 0x31, 0x41, 0x59, 0x26, 0x53, 0x59, 0x19, 0x31, 0x65, 0x3d,
    0x00, 0x00, 0x00, 0x81, 0x00, 0x02, 0x44, 0xa0, 0x00, 0x21, 0x9a, 0x68, 0x33, 0x4d,
    0x07, 0x33, 0x8b, 0xb9, 0x22, 0x9c, 0x28, 0x48, 0x0c, 0x98, 0xb2, 0x9e, 0x80};

enum { SAMPLE = 100000 };
static char in[SAMPLE];
static char packed[SAMPLE + SAMPLE / 100 + 600];
/* One spare byte: an exactly full buffer reports BZ_OUTBUFF_FULL, not
 * BZ_UNEXPECTED_EOF, for a stream cut inside its trailer. */
static char out[SAMPLE + 1];

static int fail(const char* what) {
    fprintf(stderr, "bzip2 C test failed: %s\n", what);
    return 0;
}

static int matches_the_reference_stream(void) {
    char hello[] = "hello";
    unsigned int n = sizeof packed;
    char buf[5];
    unsigned int m = sizeof buf;
    char damaged[sizeof hello_bz2];
    if (BZ2_bzBuffToBuffCompress(packed, &n, hello, 5, 9, 0, 0) != BZ_OK ||
        n != sizeof hello_bz2 || memcmp(packed, hello_bz2, n) != 0) {
        return fail("compressed bytes differ from bzip2 1.0.8's");
    }
    if (BZ2_bzBuffToBuffDecompress(buf, &m, (char*)hello_bz2, sizeof hello_bz2, 0, 0) != BZ_OK ||
        m != 5 || memcmp(buf, "hello", 5) != 0) {
        return fail("reference stream decode");
    }
    /* Bytes 10-13 hold the block's CRC. */
    memcpy(damaged, hello_bz2, sizeof damaged);
    damaged[10] ^= 0x01;
    m = sizeof buf;
    if (BZ2_bzBuffToBuffDecompress(buf, &m, damaged, sizeof damaged, 0, 0) != BZ_DATA_ERROR) {
        return fail("damaged CRC accepted");
    }
    return 1;
}

static int round_trips(int small) {
    unsigned int n = sizeof packed;
    unsigned int m = sizeof out;
    if (BZ2_bzBuffToBuffCompress(packed, &n, in, SAMPLE, 1, 0, 0) != BZ_OK || n >= SAMPLE / 8) {
        return fail("compression");
    }
    if (BZ2_bzBuffToBuffDecompress(out, &m, packed, n, small, 0) != BZ_OK || m != SAMPLE ||
        memcmp(out, in, SAMPLE) != 0) {
        return fail(small ? "small-mode round trip" : "round trip");
    }
    m = sizeof out;
    if (BZ2_bzBuffToBuffDecompress(out, &m, packed, n - 8, small, 0) != BZ_UNEXPECTED_EOF) {
        return fail("truncated stream accepted");
    }
    return 1;
}

int main(void) {
    int i;
    for (i = 0; i < SAMPLE; ++i) in[i] = "Cabin consumes cabin-ports/bzip2 from C89\n"[i % 42];
    return (strncmp(BZ2_bzlibVersion(), "1.0.8,", 6) == 0 || fail("version")) &&
                   matches_the_reference_stream() && round_trips(0) && round_trips(1)
               ? 0
               : 1;
}
