/* Checks BLAKE2b, BLAKE2s, BLAKE2bp, and BLAKE2sp against RFC 7693's "abc"
   vectors and the last keyed known-answer vectors of upstream's
   blake2-kat.h (key 0, 1, ...; message 0, 1, ..., 254), one-shot and
   streamed in chunks that straddle block boundaries, and the error returns
   for out-of-range lengths. */
#include <blake2.h>

#include <stdio.h>
#include <string.h>

static const uint8_t b2b_abc[BLAKE2B_OUTBYTES] = {
    0xba, 0x80, 0xa5, 0x3f, 0x98, 0x1c, 0x4d, 0x0d, 0x6a, 0x27, 0x97, 0xb6, 0x9f, 0x12, 0xf6, 0xe9,
    0x4c, 0x21, 0x2f, 0x14, 0x68, 0x5a, 0xc4, 0xb7, 0x4b, 0x12, 0xbb, 0x6f, 0xdb, 0xff, 0xa2, 0xd1,
    0x7d, 0x87, 0xc5, 0x39, 0x2a, 0xab, 0x79, 0x2d, 0xc2, 0x52, 0xd5, 0xde, 0x45, 0x33, 0xcc, 0x95,
    0x18, 0xd3, 0x8a, 0xa8, 0xdb, 0xf1, 0x92, 0x5a, 0xb9, 0x23, 0x86, 0xed, 0xd4, 0x00, 0x99, 0x23};
static const uint8_t b2s_abc[BLAKE2S_OUTBYTES] = {
    0x50, 0x8c, 0x5e, 0x8c, 0x32, 0x7c, 0x14, 0xe2, 0xe1, 0xa7, 0x2b, 0xa3, 0x4e, 0xeb, 0x45, 0x2f,
    0x37, 0x45, 0x8b, 0x20, 0x9e, 0xd6, 0x3a, 0x29, 0x4d, 0x99, 0x9b, 0x4c, 0x86, 0x67, 0x59, 0x82};
/* BLAKE2b-256("abc"): the digest length is a parameter, not a truncation. */
static const uint8_t b2b256_abc[32] = {
    0xbd, 0xdd, 0x81, 0x3c, 0x63, 0x42, 0x39, 0x72, 0x31, 0x71, 0xef, 0x3f, 0xee, 0x98, 0x57, 0x9b,
    0x94, 0x96, 0x4e, 0x3b, 0xb1, 0xcb, 0x3e, 0x42, 0x72, 0x62, 0xc8, 0xc0, 0x68, 0xd5, 0x23, 0x19};

static const uint8_t b2b_keyed_255[BLAKE2B_OUTBYTES] = {
    0x14, 0x27, 0x09, 0xd6, 0x2e, 0x28, 0xfc, 0xcc, 0xd0, 0xaf, 0x97, 0xfa, 0xd0, 0xf8, 0x46, 0x5b,
    0x97, 0x1e, 0x82, 0x20, 0x1d, 0xc5, 0x10, 0x70, 0xfa, 0xa0, 0x37, 0x2a, 0xa4, 0x3e, 0x92, 0x48,
    0x4b, 0xe1, 0xc1, 0xe7, 0x3b, 0xa1, 0x09, 0x06, 0xd5, 0xd1, 0x85, 0x3d, 0xb6, 0xa4, 0x10, 0x6e,
    0x0a, 0x7b, 0xf9, 0x80, 0x0d, 0x37, 0x3d, 0x6d, 0xee, 0x2d, 0x46, 0xd6, 0x2e, 0xf2, 0xa4, 0x61};
static const uint8_t b2s_keyed_255[BLAKE2S_OUTBYTES] = {
    0x3f, 0xb7, 0x35, 0x06, 0x1a, 0xbc, 0x51, 0x9d, 0xfe, 0x97, 0x9e, 0x54, 0xc1, 0xee, 0x5b, 0xfa,
    0xd0, 0xa9, 0xd8, 0x58, 0xb3, 0x31, 0x5b, 0xad, 0x34, 0xbd, 0xe9, 0x99, 0xef, 0xd7, 0x24, 0xdd};
static const uint8_t b2bp_keyed_255[BLAKE2B_OUTBYTES] = {
    0x96, 0xfb, 0xcb, 0xb6, 0x0b, 0xd3, 0x13, 0xb8, 0x84, 0x50, 0x33, 0xe5, 0xbc, 0x05, 0x8a, 0x38,
    0x02, 0x74, 0x38, 0x57, 0x2d, 0x7e, 0x79, 0x57, 0xf3, 0x68, 0x4f, 0x62, 0x68, 0xaa, 0xdd, 0x3a,
    0xd0, 0x8d, 0x21, 0x76, 0x7e, 0xd6, 0x87, 0x86, 0x85, 0x33, 0x1b, 0xa9, 0x85, 0x71, 0x48, 0x7e,
    0x12, 0x47, 0x0a, 0xad, 0x66, 0x93, 0x26, 0x71, 0x6e, 0x46, 0x66, 0x7f, 0x69, 0xf8, 0xd7, 0xe8};
static const uint8_t b2sp_keyed_255[BLAKE2S_OUTBYTES] = {
    0x0c, 0x8a, 0x36, 0x59, 0x7d, 0x74, 0x61, 0xc6, 0x3a, 0x94, 0x73, 0x28, 0x21, 0xc9, 0x41, 0x85,
    0x6c, 0x66, 0x83, 0x76, 0x60, 0x6c, 0x86, 0xa5, 0x2d, 0xe0, 0xee, 0x41, 0x04, 0xc6, 0x15, 0xdb};

/* 1 + 63 + 100 + 91 = 255 bytes; the 100-byte chunk crosses message offset
   128, a block boundary for every variant, into a partly filled buffer. */
static const size_t chunks[] = {1, 63, 100, 91};

static int failures = 0;

static void check(int ok, const char* what) {
    if (!ok) {
        fprintf(stderr, "libb2 C test failed: %s\n", what);
        ++failures;
    }
}

int main(void) {
    uint8_t key[BLAKE2B_KEYBYTES];
    uint8_t msg[255];
    uint8_t out[BLAKE2B_OUTBYTES];
    blake2b_state b;
    blake2s_state s;
    blake2bp_state bp;
    blake2sp_state sp;
    size_t i, at;

    for (i = 0; i < sizeof key; ++i)
        key[i] = (uint8_t)i;
    for (i = 0; i < sizeof msg; ++i)
        msg[i] = (uint8_t)i;

    check(blake2b(out, "abc", NULL, BLAKE2B_OUTBYTES, 3, 0) == 0 &&
              memcmp(out, b2b_abc, BLAKE2B_OUTBYTES) == 0,
          "BLAKE2b-512(\"abc\")");
    check(blake2s(out, "abc", NULL, BLAKE2S_OUTBYTES, 3, 0) == 0 &&
              memcmp(out, b2s_abc, BLAKE2S_OUTBYTES) == 0,
          "BLAKE2s-256(\"abc\")");
    check(blake2(out, "abc", NULL, 32, 3, 0) == 0 && memcmp(out, b2b256_abc, 32) == 0,
          "blake2() as BLAKE2b-256(\"abc\")");

    check(blake2b(out, msg, key, BLAKE2B_OUTBYTES, sizeof msg, BLAKE2B_KEYBYTES) == 0 &&
              memcmp(out, b2b_keyed_255, BLAKE2B_OUTBYTES) == 0,
          "keyed BLAKE2b");
    check(blake2s(out, msg, key, BLAKE2S_OUTBYTES, sizeof msg, BLAKE2S_KEYBYTES) == 0 &&
              memcmp(out, b2s_keyed_255, BLAKE2S_OUTBYTES) == 0,
          "keyed BLAKE2s");
    check(blake2bp(out, msg, key, BLAKE2B_OUTBYTES, sizeof msg, BLAKE2B_KEYBYTES) == 0 &&
              memcmp(out, b2bp_keyed_255, BLAKE2B_OUTBYTES) == 0,
          "keyed BLAKE2bp");
    check(blake2sp(out, msg, key, BLAKE2S_OUTBYTES, sizeof msg, BLAKE2S_KEYBYTES) == 0 &&
              memcmp(out, b2sp_keyed_255, BLAKE2S_OUTBYTES) == 0,
          "keyed BLAKE2sp");

    check(blake2b_init_key(&b, BLAKE2B_OUTBYTES, key, BLAKE2B_KEYBYTES) == 0 &&
              blake2s_init_key(&s, BLAKE2S_OUTBYTES, key, BLAKE2S_KEYBYTES) == 0 &&
              blake2bp_init_key(&bp, BLAKE2B_OUTBYTES, key, BLAKE2B_KEYBYTES) == 0 &&
              blake2sp_init_key(&sp, BLAKE2S_OUTBYTES, key, BLAKE2S_KEYBYTES) == 0,
          "keyed streaming init");
    for (i = 0, at = 0; i < sizeof chunks / sizeof chunks[0]; at += chunks[i++]) {
        check(blake2b_update(&b, msg + at, chunks[i]) == 0 &&
                  blake2s_update(&s, msg + at, chunks[i]) == 0 &&
                  blake2bp_update(&bp, msg + at, chunks[i]) == 0 &&
                  blake2sp_update(&sp, msg + at, chunks[i]) == 0,
              "streaming update");
    }
    check(blake2b_final(&b, out, BLAKE2B_OUTBYTES) == 0 &&
              memcmp(out, b2b_keyed_255, BLAKE2B_OUTBYTES) == 0,
          "streamed keyed BLAKE2b");
    check(blake2s_final(&s, out, BLAKE2S_OUTBYTES) == 0 &&
              memcmp(out, b2s_keyed_255, BLAKE2S_OUTBYTES) == 0,
          "streamed keyed BLAKE2s");
    check(blake2bp_final(&bp, out, BLAKE2B_OUTBYTES) == 0 &&
              memcmp(out, b2bp_keyed_255, BLAKE2B_OUTBYTES) == 0,
          "streamed keyed BLAKE2bp");
    check(blake2sp_final(&sp, out, BLAKE2S_OUTBYTES) == 0 &&
              memcmp(out, b2sp_keyed_255, BLAKE2S_OUTBYTES) == 0,
          "streamed keyed BLAKE2sp");

    check(blake2b(out, "abc", NULL, 0, 3, 0) == -1, "zero digest length is rejected");
    check(blake2b(out, "abc", NULL, BLAKE2B_OUTBYTES + 1, 3, 0) == -1,
          "BLAKE2b digest longer than 64 bytes is rejected");
    check(blake2s(out, "abc", key, BLAKE2S_OUTBYTES, 3, BLAKE2S_KEYBYTES + 1) == -1,
          "BLAKE2s key longer than 32 bytes is rejected");
    check(blake2s_init(&s, 16) == 0 && blake2s_final(&s, out, BLAKE2S_OUTBYTES) == -1,
          "final with a length other than init's is rejected");

    if (failures != 0)
        return 1;
    puts("libb2 C ok");
    return 0;
}
