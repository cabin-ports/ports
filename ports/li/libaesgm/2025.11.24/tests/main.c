/* FIPS-197 Appendix C and NIST SP 800-38A (AES-128) known answers, through
   the one-block API and the ECB, CBC, CFB, OFB and CTR modes. */
#include <aes.h>

#include <stdio.h>
#include <string.h>

static int fails;

static void hex(const char *s, unsigned char *out)
{
    for (; s[0]; s += 2) {
        unsigned v;
        sscanf(s, "%2x", &v);
        *out++ = (unsigned char)v;
    }
}

static void check(const char *what, const unsigned char *got, const char *want)
{
    unsigned char w[64];
    hex(want, w);
    if (memcmp(got, w, strlen(want) / 2) != 0) {
        printf("FAIL %s\n", what);
        fails++;
    }
}

static void ctr_inc(unsigned char *c)
{
    int i = 15;
    while (i >= 0 && ++c[i] == 0) i--;
}

static void appendix_c(int bits, const char *key_hex, const char *ct_hex)
{
    unsigned char key[32], pt[16], buf[16];
    aes_encrypt_ctx e[1];
    aes_decrypt_ctx d[1];
    hex(key_hex, key);
    hex("00112233445566778899aabbccddeeff", pt);
    if (aes_encrypt_key(key, bits, e) != EXIT_SUCCESS || aes_decrypt_key(key, bits, d) != EXIT_SUCCESS) {
        printf("FAIL key schedule %d\n", bits);
        fails++;
        return;
    }
    aes_encrypt(pt, buf, e);
    check("encrypt", buf, ct_hex);
    aes_decrypt(buf, buf, d);
    check("decrypt", buf, "00112233445566778899aabbccddeeff");
}

int main(void)
{
    unsigned char key[16], iv[16], buf[32], out[32], pt[32];
    aes_encrypt_ctx e[1];
    aes_decrypt_ctx d[1];

    aes_init();
    appendix_c(128, "000102030405060708090a0b0c0d0e0f", "69c4e0d86a7b0430d8cdb78070b4c55a");
    appendix_c(192, "000102030405060708090a0b0c0d0e0f1011121314151617", "dda97ca4864cdfe06eaf70a0ec0d7191");
    appendix_c(256, "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f",
               "8ea2b7ca516745bfeafc49904b496089");

    hex("2b7e151628aed2a6abf7158809cf4f3c", key);
    hex("6bc1bee22e409f96e93d7e117393172aae2d8a571e03ac9c9eb76fac45af8e51", pt);
    aes_encrypt_key128(key, e);
    aes_decrypt_key128(key, d);

    aes_ecb_encrypt(pt, buf, 32, e);
    check("ecb", buf, "3ad77bb40d7a3660a89ecaf32466ef97f5d3d58503b9699de785895a96fdbaaf");
    aes_ecb_decrypt(buf, out, 32, d);
    check("ecb decrypt", out, "6bc1bee22e409f96e93d7e117393172aae2d8a571e03ac9c9eb76fac45af8e51");

    hex("000102030405060708090a0b0c0d0e0f", iv);
    aes_cbc_encrypt(pt, buf, 32, iv, e);
    check("cbc", buf, "7649abac8119b246cee98e9b12e9197d5086cb9b507219ee95db113a917678b2");
    hex("000102030405060708090a0b0c0d0e0f", iv);
    aes_cbc_decrypt(buf, out, 32, iv, d);
    check("cbc decrypt", out, "6bc1bee22e409f96e93d7e117393172aae2d8a571e03ac9c9eb76fac45af8e51");

    hex("000102030405060708090a0b0c0d0e0f", iv);
    aes_mode_reset(e);
    aes_cfb_encrypt(pt, buf, 32, iv, e);
    check("cfb", buf, "3b3fd92eb72dad20333449f8e83cfb4ac8a64537a0b3a93fcde3cdad9f1ce58b");
    hex("000102030405060708090a0b0c0d0e0f", iv);
    aes_mode_reset(e);
    aes_cfb_decrypt(buf, out, 32, iv, e);
    check("cfb decrypt", out, "6bc1bee22e409f96e93d7e117393172aae2d8a571e03ac9c9eb76fac45af8e51");

    hex("000102030405060708090a0b0c0d0e0f", iv);
    aes_mode_reset(e);
    aes_ofb_crypt(pt, buf, 32, iv, e);
    check("ofb", buf, "3b3fd92eb72dad20333449f8e83cfb4a7789508d16918f03f53c52dac54ed825");

    hex("f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff", iv);
    aes_mode_reset(e);
    aes_ctr_crypt(pt, buf, 32, iv, ctr_inc, e);
    check("ctr", buf, "874d6191b620e3261bef6864990db6ce9806f66b7970fdff8617187bb9fffdff");

    if (!fails) puts("ok");
    return fails != 0;
}
