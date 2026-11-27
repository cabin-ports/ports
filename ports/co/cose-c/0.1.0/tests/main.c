/* The messages are cose-wg/Examples mac0-tests/HMac-01.json and
 * encrypted-tests/aes-gcm-01.json (public domain, the Unlicense), whose AES
 * key is the first 16 bytes of the HMAC key.  COSE_Encode writes a message
 * without its CBOR tag, the examples' first byte. */
#include <cose/cose.h>

#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(cond)                                              \
  do {                                                           \
    if (!(cond)) {                                               \
      fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #cond); \
      failures++;                                                \
    }                                                            \
  } while (0)

static const byte content[] = "This is the content.";
#define CONTENT_LEN (sizeof content - 1)

static const byte key[32] = {
    0x84, 0x9b, 0x57, 0x21, 0x9d, 0xae, 0x48, 0xde, 0x64, 0x6d, 0x07,
    0xdb, 0xb5, 0x33, 0x56, 0x6e, 0x97, 0x66, 0x86, 0x45, 0x7c, 0x14,
    0x91, 0xbe, 0x3a, 0x76, 0xdc, 0xea, 0x6c, 0x42, 0x71, 0x88};

static const byte iv[12] = {0x02, 0xd1, 0xf7, 0xe6, 0xf2, 0x6c,
                            0x43, 0xd4, 0x86, 0x8d, 0x87, 0xce};

static const byte mac0[] = {
    0xd1, 0x84, 0x43, 0xa1, 0x01, 0x05, 0xa0, 0x54, 0x54, 0x68, 0x69, 0x73,
    0x20, 0x69, 0x73, 0x20, 0x74, 0x68, 0x65, 0x20, 0x63, 0x6f, 0x6e, 0x74,
    0x65, 0x6e, 0x74, 0x2e, 0x58, 0x20, 0xa1, 0xa8, 0x48, 0xd3, 0x47, 0x1f,
    0x9d, 0x61, 0xee, 0x49, 0x01, 0x8d, 0x24, 0x4c, 0x82, 0x47, 0x72, 0xf2,
    0x23, 0xad, 0x4f, 0x93, 0x52, 0x93, 0xf1, 0x78, 0x9f, 0xc3, 0xa0, 0x8d,
    0x8c, 0x58};

static const byte encrypt0[] = {
    0xd0, 0x83, 0x43, 0xa1, 0x01, 0x01, 0xa1, 0x05, 0x4c, 0x02, 0xd1, 0xf7,
    0xe6, 0xf2, 0x6c, 0x43, 0xd4, 0x86, 0x8d, 0x87, 0xce, 0x58, 0x24, 0x60,
    0x97, 0x3a, 0x94, 0xbb, 0x28, 0x98, 0x00, 0x9e, 0xe5, 0x2e, 0xcf, 0xd9,
    0xab, 0x1d, 0xd2, 0x58, 0x67, 0x37, 0x4b, 0x16, 0x2e, 0x2c, 0x03, 0x56,
    0x8b, 0x41, 0xf5, 0x7c, 0x3c, 0xc1, 0x6f, 0x91, 0x66, 0x25, 0x0a};

/* Encodes h and compares it with the tagged example, less its tag. */
static void check_encoding(HCOSE h, const byte *tagged, size_t len) {
  byte out[128];
  size_t n = COSE_Encode(h, NULL, 0, 0);
  CHECK(n == len - 1);
  if (n == len - 1) {
    CHECK(COSE_Encode(h, out, 0, sizeof out) == n);
    CHECK(memcmp(out, tagged + 1, n) == 0);
  }
}

static void test_mac0(void) {
  cose_errback err;
  cn_cbor_errback cberr;
  byte bad_key[sizeof key];
  int type = 0;
  HCOSE_MAC0 h = COSE_Mac0_Init(COSE_INIT_FLAGS_NONE, &err);
  CHECK(h != NULL);
  if (h != NULL) {
    CHECK(COSE_Mac0_SetContent(h, content, CONTENT_LEN, &err));
    CHECK(COSE_Mac0_map_put_int(
        h, COSE_Header_Algorithm,
        cn_cbor_int_create(COSE_Algorithm_HMAC_256_256, &cberr),
        COSE_PROTECT_ONLY, &err));
    CHECK(COSE_Mac0_encrypt(h, key, sizeof key, &err));
    check_encoding((HCOSE)h, mac0, sizeof mac0);
    CHECK(COSE_Mac0_Free(h));
  }

  memcpy(bad_key, key, sizeof key);
  bad_key[31] ^= 1;
  h = (HCOSE_MAC0)COSE_Decode(mac0, sizeof mac0, &type, COSE_mac0_object,
                              &err);
  CHECK(h != NULL);
  if (h != NULL) {
    CHECK(type == COSE_mac0_object);
    CHECK(COSE_Mac0_validate(h, key, sizeof key, &err));
    CHECK(!COSE_Mac0_validate(h, bad_key, sizeof bad_key, &err));
    CHECK(COSE_Mac0_Free(h));
  }
}

static void test_encrypt0(void) {
  cose_errback err;
  cn_cbor_errback cberr;
  const byte *plain;
  size_t plain_len = 0;
  int type = 0;
  HCOSE_ENCRYPT h = COSE_Encrypt_Init(COSE_INIT_FLAGS_NONE, &err);
  CHECK(h != NULL);
  if (h != NULL) {
    CHECK(COSE_Encrypt_SetContent(h, content, CONTENT_LEN, &err));
    CHECK(COSE_Encrypt_map_put_int(
        h, COSE_Header_Algorithm,
        cn_cbor_int_create(COSE_Algorithm_AES_GCM_128, &cberr),
        COSE_PROTECT_ONLY, &err));
    CHECK(COSE_Encrypt_map_put_int(h, COSE_Header_IV,
                                   cn_cbor_data_create(iv, sizeof iv, &cberr),
                                   COSE_UNPROTECT_ONLY, &err));
    CHECK(COSE_Encrypt_encrypt(h, key, 16, &err));
    check_encoding((HCOSE)h, encrypt0, sizeof encrypt0);
    CHECK(COSE_Encrypt_Free(h));
  }

  h = (HCOSE_ENCRYPT)COSE_Decode(encrypt0, sizeof encrypt0, &type,
                                 COSE_encrypt_object, &err);
  CHECK(h != NULL);
  if (h != NULL) {
    CHECK(type == COSE_encrypt_object);
    CHECK(!COSE_Encrypt_decrypt(h, key + 16, 16, &err));
    CHECK(COSE_Encrypt_decrypt(h, key, 16, &err));
    /* The length it reports after AES-GCM is 0 (cabin.toml). */
    plain = COSE_Encrypt_GetContent(h, &plain_len, &err);
    CHECK(plain != NULL && memcmp(plain, content, CONTENT_LEN) == 0);
    CHECK(COSE_Encrypt_Free(h));
  }
}

int main(void) {
  test_mac0();
  test_encrypt0();
  if (failures == 0) {
    puts("cose-c: COSE_Mac0 and COSE_Encrypt0 match the cose-wg examples");
  }
  return failures ? 1 : 0;
}
