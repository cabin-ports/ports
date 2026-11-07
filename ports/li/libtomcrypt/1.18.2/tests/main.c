/* libtomcrypt's C API from C11 against published test vectors: SHA-256,
   SHA3-256, HMAC-SHA256 (RFC 4231), AES-128 (FIPS-197) through both the
   full and the encrypt-only descriptor, AES-GCM, base64; then, over the
   LibTomMath descriptor, a modular exponentiation and RSA and ECC keys
   drawn from sprng (rng_get_bytes). */
#include <stdio.h>
#include <string.h>

#include <tomcrypt.h>

static int failures = 0;

#define CHECK(cond)                                                    \
  do {                                                                 \
    if (!(cond)) {                                                     \
      fprintf(stderr, "main.c:%d: check failed: %s\n", __LINE__, #cond); \
      ++failures;                                                      \
    }                                                                  \
  } while (0)

/* Whether the `len` bytes at `bytes` are spelled by lower-case `hex`. */
static int hex_equals(const unsigned char *bytes, unsigned long len, const char *hex) {
  static const char digits[] = "0123456789abcdef";
  unsigned long i;
  if (strlen(hex) != 2 * len) return 0;
  for (i = 0; i < len; ++i) {
    if (hex[2 * i] != digits[bytes[i] >> 4] || hex[2 * i + 1] != digits[bytes[i] & 15]) {
      return 0;
    }
  }
  return 1;
}

int main(void) {
  static const unsigned char aes_key[16] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                                            0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};
  static const unsigned char aes_pt[16] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
                                           0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};
  const char *jefe = "what do ya want for nothing?";
  unsigned char zeros[16] = {0}, tag[16];
  unsigned char buf[512], buf2[512], hash[32];
  unsigned long len, len2;
  int sha256, prng, aes, stat;
  symmetric_key skey;
  void *a, *b, *c, *d;
  rsa_key rsa, rsa_pub;
  ecc_key alice, bob;

  CHECK((sha256 = register_hash(&sha256_desc)) >= 0);
  CHECK(register_hash(&sha3_256_desc) >= 0);
  CHECK((aes = register_cipher(&aes_desc)) >= 0);
  CHECK((prng = register_prng(&sprng_desc)) >= 0);
  CHECK(find_hash("sha256") == sha256 && find_cipher("aes") == aes && find_prng("sprng") == prng);

  /* Hashes and HMAC. */
  len = sizeof hash;
  CHECK(hash_memory(sha256, (const unsigned char *)"abc", 3, hash, &len) == CRYPT_OK && len == 32);
  CHECK(hex_equals(hash, 32, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
  len = sizeof buf;
  CHECK(hash_memory(find_hash("sha3-256"), (const unsigned char *)"abc", 3, buf, &len) == CRYPT_OK);
  CHECK(hex_equals(buf, len, "3a985da74fe225b2045c172d6bd390bd855f086e3e9d525b46bfe24511431532"));
  len = sizeof buf;
  CHECK(hmac_memory(sha256, (const unsigned char *)"Jefe", 4, (const unsigned char *)jefe,
                    strlen(jefe), buf, &len) == CRYPT_OK);
  CHECK(hex_equals(buf, len, "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843"));

  /* AES-128, FIPS-197 appendix C.1, with the full and the encrypt-only
     descriptor; the latter has no decryption. */
  CHECK(aes_setup(aes_key, 16, 0, &skey) == CRYPT_OK);
  CHECK(aes_ecb_encrypt(aes_pt, buf, &skey) == CRYPT_OK);
  CHECK(hex_equals(buf, 16, "69c4e0d86a7b0430d8cdb78070b4c55a"));
  CHECK(aes_ecb_decrypt(buf, buf2, &skey) == CRYPT_OK && memcmp(buf2, aes_pt, 16) == 0);
  memset(buf, 0, 16);
  CHECK(aes_enc_setup(aes_key, 16, 0, &skey) == CRYPT_OK);
  CHECK(aes_enc_ecb_encrypt(aes_pt, buf, &skey) == CRYPT_OK);
  CHECK(hex_equals(buf, 16, "69c4e0d86a7b0430d8cdb78070b4c55a"));
  CHECK(aes_enc_desc.ecb_encrypt == aes_enc_ecb_encrypt && aes_enc_desc.ecb_decrypt == NULL);
  CHECK(aes_setup(aes_key, 15, 0, &skey) == CRYPT_INVALID_KEYSIZE);
  CHECK(strcmp(error_to_string(CRYPT_INVALID_KEYSIZE), "Invalid key size.") == 0);

  /* AES-GCM, test case 2 of the GCM specification. */
  len = 16;
  CHECK(gcm_memory(aes, zeros, 16, zeros, 12, NULL, 0, zeros, 16, buf, tag, &len, GCM_ENCRYPT) ==
        CRYPT_OK);
  CHECK(hex_equals(buf, 16, "0388dace60b6a392f328c2b971b2fe78"));
  CHECK(hex_equals(tag, len, "ab6e47d42cec13bdf53a67b21257bddf"));
  len = 16;
  memset(tag, 0, sizeof tag);
  CHECK(gcm_memory(aes, zeros, 16, zeros, 12, NULL, 0, buf2, 16, buf, tag, &len, GCM_DECRYPT) ==
        CRYPT_OK);
  CHECK(memcmp(buf2, zeros, 16) == 0 && hex_equals(tag, len, "ab6e47d42cec13bdf53a67b21257bddf"));

  /* base64. */
  len = sizeof buf;
  CHECK(base64_encode((const unsigned char *)"foobar", 6, buf, &len) == CRYPT_OK);
  CHECK(len == 8 && memcmp(buf, "Zm9vYmFy", 8) == 0);
  len2 = sizeof buf2;
  CHECK(base64_decode(buf, len, buf2, &len2) == CRYPT_OK && len2 == 6 &&
        memcmp(buf2, "foobar", 6) == 0);

  /* Public-key math through LibTomMath. */
  ltc_mp = ltm_desc;
  CHECK(ltc_init_multi(&a, &b, &c, &d, NULL) == CRYPT_OK);
  CHECK(ltc_mp.read_radix(a, "4", 10) == CRYPT_OK && ltc_mp.read_radix(b, "13", 10) == CRYPT_OK &&
        ltc_mp.read_radix(c, "497", 10) == CRYPT_OK);
  CHECK(ltc_mp.exptmod(a, b, c, d) == CRYPT_OK && ltc_mp.compare_d(d, 445) == LTC_MP_EQ);
  ltc_deinit_multi(a, b, c, d, NULL);

  /* RSA-1024: PSS signature over SHA-256("abc"), OAEP round trip, and the
     public key through DER. */
  CHECK(rsa_make_key(NULL, prng, 128, 65537, &rsa) == CRYPT_OK);
  CHECK(rsa_get_size(&rsa) == 128);
  len = sizeof buf;
  CHECK(rsa_sign_hash_ex(hash, 32, buf, &len, LTC_PKCS_1_PSS, NULL, prng, sha256, 16, &rsa) ==
        CRYPT_OK);
  len2 = sizeof buf2;
  CHECK(rsa_export(buf2, &len2, PK_PUBLIC, &rsa) == CRYPT_OK);
  CHECK(rsa_import(buf2, len2, &rsa_pub) == CRYPT_OK);
  stat = 0;
  CHECK(rsa_verify_hash_ex(buf, len, hash, 32, LTC_PKCS_1_PSS, sha256, 16, &stat, &rsa_pub) ==
            CRYPT_OK &&
        stat == 1);
  hash[0] ^= 1;
  stat = 1;
  CHECK(rsa_verify_hash_ex(buf, len, hash, 32, LTC_PKCS_1_PSS, sha256, 16, &stat, &rsa_pub) ==
            CRYPT_OK &&
        stat == 0);
  len = sizeof buf;
  CHECK(rsa_encrypt_key(aes_key, 16, buf, &len, NULL, 0, NULL, prng, sha256, &rsa_pub) == CRYPT_OK);
  len2 = sizeof buf2;
  stat = 0;
  CHECK(rsa_decrypt_key(buf, len, buf2, &len2, NULL, 0, sha256, &stat, &rsa) == CRYPT_OK &&
        stat == 1 && len2 == 16 && memcmp(buf2, aes_key, 16) == 0);
  rsa_free(&rsa_pub);
  rsa_free(&rsa);

  /* ECC-256: both parties derive one secret; ECDSA verifies, and a
     changed hash does not. */
  CHECK(ecc_make_key(NULL, prng, 32, &alice) == CRYPT_OK);
  CHECK(ecc_make_key(NULL, prng, 32, &bob) == CRYPT_OK);
  len = sizeof buf;
  len2 = sizeof buf2;
  CHECK(ecc_shared_secret(&alice, &bob, buf, &len) == CRYPT_OK);
  CHECK(ecc_shared_secret(&bob, &alice, buf2, &len2) == CRYPT_OK);
  CHECK(len == 32 && len2 == 32 && memcmp(buf, buf2, 32) == 0);
  len = sizeof buf;
  CHECK(ecc_sign_hash(hash, 32, buf, &len, NULL, prng, &alice) == CRYPT_OK);
  stat = 0;
  CHECK(ecc_verify_hash(buf, len, hash, 32, &stat, &alice) == CRYPT_OK && stat == 1);
  hash[0] ^= 1;
  stat = 1;
  CHECK(ecc_verify_hash(buf, len, hash, 32, &stat, &alice) == CRYPT_OK && stat == 0);
  ecc_free(&bob);
  ecc_free(&alice);

  if (failures != 0) {
    fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  return 0;
}
