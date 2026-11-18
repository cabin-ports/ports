#include <wyhash.h>

#include <cstdio>
#include <cstring>

static int failures = 0;

static void check(int ok, const char *what)
{
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

/* SMHasher's VerificationTest (KeysetTest.cpp): hash {}, {0}, {0, 1}, ...
   {0, ..., 254} with seed 256 - length, then hash the little-endian
   results with seed 0 and keep the low 32 bits. */
static uint32_t smhasher_verification(void)
{
    uint8_t key[256];
    uint8_t hashes[8 * 256];
    int i, j;
    for (i = 0; i < 256; i++) {
        uint64_t h;
        key[i] = (uint8_t)i;
        h = wyhash(key, (size_t)i, (uint64_t)(256 - i), _wyp);
        for (j = 0; j < 8; j++)
            hashes[i * 8 + j] = (uint8_t)(h >> (8 * j));
    }
    return (uint32_t)wyhash(hashes, sizeof(hashes), 0, _wyp);
}

int main(void)
{
    /* test_vector.cpp's messages.  These values, the wyrand, wyhash64 and
       make_secret ones, come from an independent model of final4's algorithm. */
    static const char *const messages[] = {
        "",
        "a",
        "abc",
        "message digest",
        "abcdefghijklmnopqrstuvwxyz",
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789",
        "12345678901234567890123456789012345678901234567890123456789012345678901234567890",
    };
    static const uint64_t expected[] = {
        0x0409638ee2bde459ull, 0xa8412d091b5fe0a9ull, 0x32dd92e4b2915153ull, 0x8619124089a3a16bull,
        0x7a43afb61d7f5f40ull, 0xff42329b90e50d58ull, 0xc39cab13b115aad3ull,
    };
    static const uint64_t expected_secret[] = {
        0x4d781d729a998b95ull, 0xa52e8ec66a3c5655ull, 0xb4e89c6536272da3ull, 0x6aacaaac8ee2c393ull,
    };
    uint64_t seed = 1;
    uint64_t secret[4];
    size_t i;

    for (i = 0; i < 7; i++)
        check(wyhash(messages[i], std::strlen(messages[i]), i, _wyp) == expected[i], i == 0 ? "empty message" : messages[i]);

    /* rurban/smhasher main.cpp lists 0xBD5E840C for wyhash at 9b6d1cecef. */
    check(smhasher_verification() == 0xBD5E840Cu, "SMHasher verification value");

    check(wyrand(&seed) == 0xcdef1695e1f8ed2cull, "first wyrand");
    check(seed == 0xa0761d6478bd6430ull, "wyrand advances the seed");
    check(wyrand(&seed) == 0x61d6d24b1c9aad40ull, "second wyrand");
    check(wyrand(&seed) == 0x8cf880c22eebfadfull, "third wyrand");
    check(wyhash64(1, 2) == 0x12f633807d6dd48cull, "wyhash64");
    check(wy2u0k(0x8000000000000000ull, 1000) == 500, "wy2u0k at one half");
    check(wy2u0k(0xffffffffffffffffull, 1000) == 999, "wy2u0k at the top");
    check(wy2u01(0x8000000000000000ull) == 0.5, "wy2u01 at one half");

    make_secret(42, secret);
    for (i = 0; i < 4; i++)
        check(secret[i] == expected_secret[i], "make_secret");
    check(wyhash("abc", 3, 0, secret) == 0xd7c09c6bd46b06a1ull, "wyhash with a custom secret");

    if (failures != 0)
        return 1;
    std::puts("wyhash consumer test passed");
    return 0;
}
