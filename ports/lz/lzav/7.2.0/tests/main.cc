/* C++ twin of main.c: the same checks through the header's C++ face. */
#include <lzav.h>

#include <stdint.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static int failures = 0;

#define CHECK(cond)                                                         \
    do {                                                                    \
        if (!(cond)) {                                                      \
            printf("%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                     \
        }                                                                   \
    } while (0)

static void round_trip(const uint8_t *src, int len, int expect_smaller) {
    int bound = lzav_compress_bound(len);
    int bound_hi = lzav_compress_bound_hi(len);
    uint8_t *comp = static_cast<uint8_t *>(malloc((size_t)bound_hi));
    uint8_t *back = static_cast<uint8_t *>(malloc((size_t)len));
    int level;
    CHECK(comp != NULL && back != NULL);
    CHECK(bound >= len);
    for (level = 0; level < 2; ++level) {
        int clen = level == 0 ? lzav_compress_default(src, comp, len, bound)
                              : lzav_compress_hi(src, comp, len, bound_hi);
        CHECK(clen > 0);
        if (expect_smaller) CHECK(clen < len / 4);
        memset(back, 0, (size_t)len);
        CHECK(lzav_decompress(comp, back, clen, len) == len);
        CHECK(memcmp(src, back, (size_t)len) == 0);
        /* A wrong expected length is an error. */
        CHECK(lzav_decompress(comp, back, clen, len - 1) < 0);
        /* A source cut short is an error. */
        CHECK(lzav_decompress(comp, back, clen - 1, len) < 0);
    }
    free(comp);
    free(back);
}

int main() {
    enum { N = 4096 };
    uint8_t text[N], noise[N], comp[N * 2], back[N];
    uint32_t x = 12345u;
    int i, clen;

    for (i = 0; i < N; ++i) {
        text[i] = static_cast<uint8_t>("lzav round trip "[i % 16]);
        x = x * 1664525u + 1013904223u;
        noise[i] = static_cast<uint8_t>(x >> 24);
    }
    round_trip(text, N, 1);
    round_trip(noise, N, 0);
    round_trip(text, 1, 0);

    /* A destination too small for the output compresses to 0. */
    CHECK(lzav_compress_default(noise, comp, N, 16) == 0);
    CHECK(lzav_compress_default(noise, comp, 0, (int)sizeof comp) == 0);

    clen = lzav_compress_default(text, comp, N, (int)sizeof comp);
    CHECK(clen > 0);
    CHECK(lzav_decompress(comp, back, -1, N) == LZAV_E_PARAMS);
    CHECK(lzav_decompress(comp, back, clen, N - 1) < 0);
    /* The high nibble of the first byte is the data format. */
    comp[0] = static_cast<uint8_t>(0xF0 | (comp[0] & 0x0F));
    CHECK(lzav_decompress(comp, back, clen, N) == LZAV_E_UNKFMT);

    CHECK(strcmp(LZAV_VER_STR, "7.2") == 0);
    if (failures == 0) printf("ok\n");
    return failures != 0;
}
