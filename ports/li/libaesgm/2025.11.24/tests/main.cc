// FIPS-197 Appendix C known answers through aescpp.h's AESencrypt and
// AESdecrypt, plus an NIST SP 800-38A CBC-AES128 vector.
#include <aescpp.h>

#include <cstdio>
#include <cstring>

static int fails;

static void hex(const char *s, unsigned char *out)
{
    for (; s[0]; s += 2) {
        unsigned v;
        std::sscanf(s, "%2x", &v);
        *out++ = static_cast<unsigned char>(v);
    }
}

static void check(const char *what, const unsigned char *got, const char *want)
{
    unsigned char w[64];
    hex(want, w);
    if (std::memcmp(got, w, std::strlen(want) / 2) != 0) {
        std::printf("FAIL %s\n", what);
        fails++;
    }
}

static void appendix_c(int bits, const char *key_hex, const char *ct_hex)
{
    unsigned char key[32], pt[16], buf[16];
    AESencrypt enc;
    AESdecrypt dec;
    hex(key_hex, key);
    hex("00112233445566778899aabbccddeeff", pt);
    if (enc.key(key, bits) != EXIT_SUCCESS || dec.key(key, bits) != EXIT_SUCCESS) {
        std::printf("FAIL key schedule %d\n", bits);
        fails++;
        return;
    }
    enc.encrypt(pt, buf);
    check("encrypt", buf, ct_hex);
    dec.decrypt(buf, buf);
    check("decrypt", buf, "00112233445566778899aabbccddeeff");
}

int main()
{
    appendix_c(128, "000102030405060708090a0b0c0d0e0f", "69c4e0d86a7b0430d8cdb78070b4c55a");
    appendix_c(192, "000102030405060708090a0b0c0d0e0f1011121314151617", "dda97ca4864cdfe06eaf70a0ec0d7191");
    appendix_c(256, "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f",
               "8ea2b7ca516745bfeafc49904b496089");

    unsigned char key[16], iv[16], pt[32], buf[32], out[32];
    hex("2b7e151628aed2a6abf7158809cf4f3c", key);
    hex("6bc1bee22e409f96e93d7e117393172aae2d8a571e03ac9c9eb76fac45af8e51", pt);
    AESencrypt enc(key);
    AESdecrypt dec(key);
    hex("000102030405060708090a0b0c0d0e0f", iv);
    enc.cbc_encrypt(pt, buf, 32, iv);
    check("cbc", buf, "7649abac8119b246cee98e9b12e9197d5086cb9b507219ee95db113a917678b2");
    hex("000102030405060708090a0b0c0d0e0f", iv);
    dec.cbc_decrypt(buf, out, 32, iv);
    check("cbc decrypt", out, "6bc1bee22e409f96e93d7e117393172aae2d8a571e03ac9c9eb76fac45af8e51");

    if (!fails) std::puts("ok");
    return fails != 0;
}
