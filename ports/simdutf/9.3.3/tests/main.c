/* The C consumer, through include/simdutf_c.h: UTF-8 validation with
 * error positions, transcoding between UTF-8, UTF-16, UTF-32 and Latin-1,
 * lengths, RFC 4648 base64, on short inputs and on inputs long enough for
 * the run-time-selected SIMD kernel.  C99. */
#include <simdutf_c.h>

#include <stdio.h>
#include <string.h>

static int failures = 0;

static void check(int ok, const char *what)
{
    if (!ok) {
        fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

/* "héllo, 世界 😀": 19 bytes, 11 code points, 12 UTF-16 code units. */
static const char sample[] = "h\xc3\xa9llo, \xe4\xb8\x96\xe7\x95\x8c \xf0\x9f\x98\x80";
static const unsigned long sample_cp[11] = {0x68, 0xe9, 0x6c, 0x6c, 0x6f, 0x2c,
                                            0x20, 0x4e16, 0x754c, 0x20, 0x1f600};
static const unsigned sample_u16[12] = {0x68, 0xe9, 0x6c, 0x6c, 0x6f, 0x2c,
                                        0x20, 0x4e16, 0x754c, 0x20, 0xd83d, 0xde00};

enum { REPEAT = 256, LONG_LEN = 19 * REPEAT };

static void short_strings(void)
{
    char16_t u16[12];
    char32_t u32[11];
    char back[19];
    unsigned char le[24];
    int ok = 1;
    size_t i;

    check(sizeof sample - 1 == 19 && simdutf_validate_utf8(sample, 19), "valid UTF-8");
    check(simdutf_count_utf8(sample, 19) == 11, "code points");
    check(simdutf_utf16_length_from_utf8(sample, 19) == 12, "UTF-16 length");
    check(simdutf_utf32_length_from_utf8(sample, 19) == 11, "UTF-32 length");

    check(simdutf_convert_utf8_to_utf16(sample, 19, u16) == 12, "UTF-8 to UTF-16");
    for (i = 0; i < 12; ++i) ok = ok && u16[i] == sample_u16[i];
    check(ok, "UTF-16 code units, surrogate pair included");
    check(simdutf_validate_utf16(u16, 12) && simdutf_utf8_length_from_utf16(u16, 12) == 19 &&
              simdutf_convert_utf16_to_utf8(u16, 12, back) == 19 && memcmp(back, sample, 19) == 0,
          "UTF-16 back to UTF-8");

    /* The LE variant writes little-endian code units whatever the host. */
    check(simdutf_convert_utf8_to_utf16le(sample, 19, u16) == 12, "UTF-8 to UTF-16LE");
    memcpy(le, u16, sizeof le);
    check(memcmp(le + 14, "\x16\x4e\x4c\x75\x20\x00\x3d\xd8\x00\xde", 10) == 0,
          "UTF-16LE bytes");

    ok = 1;
    check(simdutf_convert_utf8_to_utf32(sample, 19, u32) == 11, "UTF-8 to UTF-32");
    for (i = 0; i < 11; ++i) ok = ok && u32[i] == sample_cp[i];
    check(ok, "UTF-32 code points");
    check(simdutf_utf8_length_from_utf32(u32, 11) == 19 &&
              simdutf_convert_utf32_to_utf8(u32, 11, back) == 19 && memcmp(back, sample, 19) == 0,
          "UTF-32 back to UTF-8");
    u32[3] = 0xd800;
    check(!simdutf_validate_utf32(u32, 11), "a surrogate is not a UTF-32 scalar value");
    u32[3] = 0x110000;
    check(!simdutf_validate_utf32(u32, 11), "past U+10FFFF");

    check(simdutf_utf8_length_from_latin1("caf\xe9", 4) == 5 &&
              simdutf_convert_latin1_to_utf8("caf\xe9", 4, back) == 5 &&
              memcmp(back, "caf\xc3\xa9", 5) == 0,
          "Latin-1 to UTF-8");

    {
        /* A lone high surrogate becomes U+FFFD. */
        const char16_t lone[3] = {0x41, 0xd800, 0x42};
        char16_t fixed[3];
        check(!simdutf_validate_utf16(lone, 3), "lone surrogate rejected");
        simdutf_to_well_formed_utf16(lone, 3, fixed);
        check(fixed[0] == 0x41 && fixed[1] == 0xfffd && fixed[2] == 0x42, "well-formed UTF-16");
    }
}

static void utf8_errors(void)
{
    static const struct { const char *s; size_t len, pos; simdutf_error_code err; } bad[] = {
        {"ab\xc3", 3, 2, SIMDUTF_ERROR_TOO_SHORT},
        {"a\x80", 2, 1, SIMDUTF_ERROR_TOO_LONG},
        {"\xc0\xaf", 2, 0, SIMDUTF_ERROR_OVERLONG},
        {"x\xf4\x90\x80\x80", 5, 1, SIMDUTF_ERROR_TOO_LARGE},
        {"xy\xed\xa0\x80", 5, 2, SIMDUTF_ERROR_SURROGATE},
        {"\xf8\x88\x80\x80\x80", 5, 0, SIMDUTF_ERROR_HEADER_BITS},
    };
    size_t i;

    for (i = 0; i < sizeof bad / sizeof bad[0]; ++i) {
        simdutf_result r = simdutf_validate_utf8_with_errors(bad[i].s, bad[i].len);
        check(!simdutf_validate_utf8(bad[i].s, bad[i].len) && r.error == bad[i].err &&
                  r.count == bad[i].pos,
              "UTF-8 error kind and position");
    }
    check(simdutf_validate_ascii("plain", 5) && !simdutf_validate_ascii("caf\xc3\xa9", 5),
          "ASCII");
}

static void long_input(void)
{
    static char text[LONG_LEN], back[LONG_LEN];
    static char16_t u16[12 * REPEAT];
    simdutf_result r;
    int i;

    for (i = 0; i < REPEAT; ++i) memcpy(text + 19 * i, sample, 19);
    check(simdutf_validate_utf8(text, LONG_LEN) &&
              simdutf_count_utf8(text, LONG_LEN) == 11 * REPEAT &&
              simdutf_utf16_length_from_utf8(text, LONG_LEN) == 12 * REPEAT,
          "long valid input");
    check(simdutf_convert_utf8_to_utf16(text, LONG_LEN, u16) == 12 * REPEAT &&
              u16[12 * 200 + 7] == 0x4e16 &&
              simdutf_convert_utf16_to_utf8(u16, 12 * REPEAT, back) == LONG_LEN &&
              memcmp(back, text, LONG_LEN) == 0,
          "long round trip");
    check(simdutf_detect_encodings(text, LONG_LEN) & SIMDUTF_ENCODING_UTF8, "detected as UTF-8");

    /* Cut the é of copy 200 short: the error is its lead byte. */
    text[19 * 200 + 2] = 'A';
    r = simdutf_validate_utf8_with_errors(text, LONG_LEN);
    check(!simdutf_validate_utf8(text, LONG_LEN) && r.error == SIMDUTF_ERROR_TOO_SHORT &&
              r.count == 19 * 200 + 1,
          "long input error position");
    r = simdutf_convert_utf8_to_utf16_with_errors(text, LONG_LEN, u16);
    check(r.error == SIMDUTF_ERROR_TOO_SHORT && r.count == 19 * 200 + 1,
          "conversion stops at the error");
}

static void base64(void)
{
    static const char *const plain[] = {"", "f", "fo", "foo", "foob", "fooba", "foobar"};
    static const char *const coded[] = {"", "Zg==", "Zm8=", "Zm9v", "Zm9vYg==", "Zm9vYmE=",
                                        "Zm9vYmFy"};
    static char bytes[3000], text[4000], out[3000];
    char buf[16];
    simdutf_result r;
    size_t i, n;
    unsigned seed = 1;

    for (i = 0; i < 7; ++i) {
        n = simdutf_binary_to_base64(plain[i], strlen(plain[i]), buf, SIMDUTF_BASE64_DEFAULT);
        check(n == strlen(coded[i]) && memcmp(buf, coded[i], n) == 0 &&
                  simdutf_base64_length_from_binary(strlen(plain[i]), SIMDUTF_BASE64_DEFAULT) == n,
              "RFC 4648 encoding");
        r = simdutf_base64_to_binary(coded[i], n, buf, SIMDUTF_BASE64_DEFAULT,
                                     SIMDUTF_LAST_CHUNK_LOOSE);
        check(r.error == SIMDUTF_ERROR_SUCCESS && r.count == strlen(plain[i]) &&
                  memcmp(buf, plain[i], r.count) == 0,
              "RFC 4648 decoding");
    }
    n = simdutf_binary_to_base64("\xfb\xff", 2, buf, SIMDUTF_BASE64_DEFAULT);
    check(n == 4 && memcmp(buf, "+/8=", 4) == 0, "standard alphabet");
    n = simdutf_binary_to_base64("\xfb\xff", 2, buf, SIMDUTF_BASE64_URL);
    check(n == 3 && memcmp(buf, "-_8", 3) == 0, "URL alphabet, unpadded");
    r = simdutf_base64_to_binary("Zm9v!mFy", 8, buf, SIMDUTF_BASE64_DEFAULT,
                                 SIMDUTF_LAST_CHUNK_LOOSE);
    check(r.error == SIMDUTF_ERROR_INVALID_BASE64_CHARACTER && r.count == 4,
          "invalid character and its position");

    for (i = 0; i < sizeof bytes; ++i) {
        seed = seed * 1103515245u + 12345u;
        bytes[i] = (char)(seed >> 16);
    }
    n = simdutf_binary_to_base64(bytes, sizeof bytes, text, SIMDUTF_BASE64_DEFAULT);
    r = simdutf_base64_to_binary(text, n, out, SIMDUTF_BASE64_DEFAULT, SIMDUTF_LAST_CHUNK_STRICT);
    check(n == 4000 && r.error == SIMDUTF_ERROR_SUCCESS && r.count == sizeof bytes &&
              memcmp(out, bytes, sizeof bytes) == 0,
          "long base64 round trip");
}

int main(void)
{
    short_strings();
    utf8_errors();
    long_input();
    base64();
    if (failures != 0) return 1;
    puts("simdutf consumer ok");
    return 0;
}
