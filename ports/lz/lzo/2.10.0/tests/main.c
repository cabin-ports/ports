/* Checks the checksums against their standard check values, LZO1X's
 * stream format on hand-made blocks, lzo1x_1 and lzo1x_999 round trips on
 * structured and random data, and the errors lzo1x_decompress_safe
 * reports for damaged input or too small an output buffer. */
#include <lzo/lzo1x.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DATA_LEN 65536UL
#define BOUND(n) ((n) + (n) / 16 + 64 + 3)

static int ok = 1;

static void check(int pass, const char *what)
{
    if (!pass) {
        fprintf(stderr, "FAIL: %s\n", what);
        ok = 0;
    }
}

static int same(const unsigned char *a, lzo_uint a_len, const unsigned char *b, lzo_uint b_len)
{
    return a_len == b_len && memcmp(a, b, a_len) == 0;
}

static void round_trip(const char *name, const unsigned char *data, lzo_uint len, int level_999,
                       unsigned char *packed, lzo_uint *packed_len, unsigned char *unpacked,
                       void *wrkmem)
{
    lzo_uint unpacked_len = len;
    int r = level_999 ? lzo1x_999_compress(data, len, packed, packed_len, wrkmem)
                      : lzo1x_1_compress(data, len, packed, packed_len, wrkmem);
    check(r == LZO_E_OK && *packed_len <= BOUND(len), name);
    r = lzo1x_decompress_safe(packed, *packed_len, unpacked, &unpacked_len, NULL);
    check(r == LZO_E_OK && same(unpacked, unpacked_len, data, len), name);
}

int main(void)
{
    static const unsigned char hello[] = "hello, lzo";
    /* Inputs of at most 20 bytes become one literal run (17 + length,
     * then the bytes) and the end-of-stream marker. */
    static const unsigned char hello_packed[] = {0x1b, 'h', 'e', 'l', 'l', 'o', ',', ' ',
                                                 'l',  'z', 'o', 0x11, 0x00, 0x00};
    /* Four literals, then an M2 match of four bytes at distance four. */
    static const unsigned char match_packed[] = {0x15, 'a', 'b', 'c', 'd', 0x6c, 0x00,
                                                 0x11, 0x00, 0x00};
    /* The same literals, then a match 2048 bytes back. */
    static const unsigned char behind_packed[] = {0x15, 'a', 'b', 'c', 'd', 0xfc, 0xff,
                                                  0x11, 0x00, 0x00};
    unsigned char small[64];
    lzo_uint small_len;
    unsigned char *text, *noise, *packed, *packed_999, *unpacked;
    void *wrkmem;
    lzo_uint packed_len, packed_999_len, noise_len, unpacked_len;
    lzo_uint32_t adler, crc;
    unsigned long seed = 12345;
    lzo_uint i;
    int r;

    check(lzo_init() == LZO_E_OK, "lzo_init");
    check(lzo_version() == LZO_VERSION && strcmp(lzo_version_string(), "2.10") == 0,
          "version");

    /* Checksums: the standard check values, and incremental updates. */
    check(lzo_adler32(0, NULL, 0) == 1 && lzo_crc32(0, NULL, 0) == 0, "checksum seeds");
    check(lzo_adler32(1, (const lzo_bytep) "Wikipedia", 9) == 0x11e60398UL, "lzo_adler32");
    check(lzo_crc32(0, (const lzo_bytep) "123456789", 9) == 0xcbf43926UL, "lzo_crc32");
    adler = lzo_adler32(lzo_adler32(1, hello, 4), hello + 4, 6);
    crc = lzo_crc32(lzo_crc32(0, hello, 4), hello + 4, 6);
    check(adler == 0x149803b6UL && crc == 0xe8ad7439UL, "incremental checksums");

    text = (unsigned char *) malloc(DATA_LEN + 1);
    noise = (unsigned char *) malloc(DATA_LEN);
    packed = (unsigned char *) malloc(BOUND(DATA_LEN));
    packed_999 = (unsigned char *) malloc(BOUND(DATA_LEN));
    unpacked = (unsigned char *) malloc(DATA_LEN + 1);
    wrkmem = malloc(LZO1X_999_MEM_COMPRESS);
    if (!text || !noise || !packed || !packed_999 || !unpacked || !wrkmem) {
        fprintf(stderr, "out of memory\n");
        return 1;
    }

    /* The stream format, both ways. */
    r = lzo1x_1_compress(hello, 10, small, &small_len, wrkmem);
    check(r == LZO_E_OK && same(small, small_len, hello_packed, sizeof hello_packed),
          "lzo1x_1 output for a short input");
    small_len = sizeof small;
    r = lzo1x_decompress_safe(match_packed, sizeof match_packed, small, &small_len, NULL);
    check(r == LZO_E_OK && same(small, small_len, (const unsigned char *) "abcdabcd", 8),
          "decoding a match");
    small_len = sizeof small;
    r = lzo1x_decompress_safe(behind_packed, sizeof behind_packed, small, &small_len, NULL);
    check(r == LZO_E_LOOKBEHIND_OVERRUN, "a match before the output's start");

    /* Structured data: numbered 32-byte lines, which compress well. */
    for (i = 0; i < DATA_LEN; i += 32)
        sprintf((char *) text + i, "record %08lu: lzo test data.\n", (unsigned long) (i / 32));
    /* Random data, which does not. */
    for (i = 0; i < DATA_LEN; ++i) {
        seed = (seed * 1103515245UL + 12345UL) & 0xffffffffUL;
        noise[i] = (unsigned char) (seed >> 16);
    }
    /* Long enough for both Adler-32 sums to wrap modulo 65521 and for the
     * checksums' 16-byte loops. */
    check(lzo_adler32(1, text, DATA_LEN) == 0x8a227370UL &&
              lzo_crc32(0, text, DATA_LEN) == 0x51aa8a95UL,
          "checksums of 64 KiB");

    round_trip("lzo1x_1 on structured data", text, DATA_LEN, 0, packed, &packed_len, unpacked,
               wrkmem);
    round_trip("lzo1x_999 on structured data", text, DATA_LEN, 1, packed_999, &packed_999_len,
               unpacked, wrkmem);
    check(packed_len < DATA_LEN / 4 && packed_999_len <= packed_len,
          "structured data compresses, lzo1x_999 at least as well");
    round_trip("lzo1x_1 on random data", noise, DATA_LEN, 0, packed, &noise_len, unpacked,
               wrkmem);
    round_trip("lzo1x_999 on random data", noise, DATA_LEN, 1, packed, &noise_len, unpacked,
               wrkmem);

    /* Damaged input and short output, on the lzo1x_999 block. */
    unpacked_len = DATA_LEN;
    r = lzo1x_decompress_safe(packed_999, packed_999_len - 1, unpacked, &unpacked_len, NULL);
    check(r == LZO_E_INPUT_OVERRUN, "a truncated block");
    packed_999[packed_999_len] = 0;
    unpacked_len = DATA_LEN;
    r = lzo1x_decompress_safe(packed_999, packed_999_len + 1, unpacked, &unpacked_len, NULL);
    check(r == LZO_E_INPUT_NOT_CONSUMED, "trailing input");
    unpacked_len = DATA_LEN - 1;
    r = lzo1x_decompress_safe(packed_999, packed_999_len, unpacked, &unpacked_len, NULL);
    check(r == LZO_E_OUTPUT_OVERRUN, "an output buffer one byte short");

    free(text);
    free(noise);
    free(packed);
    free(packed_999);
    free(unpacked);
    free(wrkmem);
    printf("lzo (C): %s\n", ok ? "ok" : "FAILED");
    return ok ? 0 : 1;
}
