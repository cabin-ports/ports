// Round trips through COSE_Encode and COSE_Decode with the inputs of
// cose-wg/Examples mac0-tests/HMac-01.json and encrypted-tests/aes-gcm-01.json
// (public domain, the Unlicense).  COSE_Encode writes a message without its
// CBOR tag, so the expected bytes are the examples' outputs less the first.
#include <cose/cose.h>

#include <cstdio>
#include <cstring>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, int line) {
  if (!ok) {
    std::fprintf(stderr, "main.cc:%d: check failed\n", line);
    ++failures;
  }
}
#define CHECK(cond) check((cond), __LINE__)

const byte content[] = "This is the content.";
const size_t content_len = sizeof content - 1;

// The HMAC key; the AES-128 key is its first 16 bytes.
const byte key[32] = {0x84, 0x9b, 0x57, 0x21, 0x9d, 0xae, 0x48, 0xde,
                      0x64, 0x6d, 0x07, 0xdb, 0xb5, 0x33, 0x56, 0x6e,
                      0x97, 0x66, 0x86, 0x45, 0x7c, 0x14, 0x91, 0xbe,
                      0x3a, 0x76, 0xdc, 0xea, 0x6c, 0x42, 0x71, 0x88};
const byte iv[12] = {0x02, 0xd1, 0xf7, 0xe6, 0xf2, 0x6c,
                     0x43, 0xd4, 0x86, 0x8d, 0x87, 0xce};

const std::vector<byte> mac0 = {
    0x84, 0x43, 0xa1, 0x01, 0x05, 0xa0, 0x54, 0x54, 0x68, 0x69, 0x73,
    0x20, 0x69, 0x73, 0x20, 0x74, 0x68, 0x65, 0x20, 0x63, 0x6f, 0x6e,
    0x74, 0x65, 0x6e, 0x74, 0x2e, 0x58, 0x20, 0xa1, 0xa8, 0x48, 0xd3,
    0x47, 0x1f, 0x9d, 0x61, 0xee, 0x49, 0x01, 0x8d, 0x24, 0x4c, 0x82,
    0x47, 0x72, 0xf2, 0x23, 0xad, 0x4f, 0x93, 0x52, 0x93, 0xf1, 0x78,
    0x9f, 0xc3, 0xa0, 0x8d, 0x8c, 0x58};

const std::vector<byte> encrypt0 = {
    0x83, 0x43, 0xa1, 0x01, 0x01, 0xa1, 0x05, 0x4c, 0x02, 0xd1, 0xf7, 0xe6,
    0xf2, 0x6c, 0x43, 0xd4, 0x86, 0x8d, 0x87, 0xce, 0x58, 0x24, 0x60, 0x97,
    0x3a, 0x94, 0xbb, 0x28, 0x98, 0x00, 0x9e, 0xe5, 0x2e, 0xcf, 0xd9, 0xab,
    0x1d, 0xd2, 0x58, 0x67, 0x37, 0x4b, 0x16, 0x2e, 0x2c, 0x03, 0x56, 0x8b,
    0x41, 0xf5, 0x7c, 0x3c, 0xc1, 0x6f, 0x91, 0x66, 0x25, 0x0a};

std::vector<byte> encode(HCOSE h) {
  std::vector<byte> out(COSE_Encode(h, nullptr, 0, 0));
  out.resize(COSE_Encode(h, out.data(), 0, out.size()));
  return out;
}

void test_mac0() {
  cose_errback err;
  cn_cbor_errback cberr;
  std::vector<byte> wire;
  HCOSE_MAC0 h = COSE_Mac0_Init(COSE_INIT_FLAGS_NONE, &err);
  CHECK(h != nullptr);
  if (h == nullptr) return;
  CHECK(COSE_Mac0_SetContent(h, content, content_len, &err));
  CHECK(COSE_Mac0_map_put_int(
      h, COSE_Header_Algorithm,
      cn_cbor_int_create(COSE_Algorithm_HMAC_256_256, &cberr),
      COSE_PROTECT_ONLY, &err));
  CHECK(COSE_Mac0_encrypt(h, key, sizeof key, &err));
  wire = encode(reinterpret_cast<HCOSE>(h));
  CHECK(wire == mac0);
  CHECK(COSE_Mac0_Free(h));

  byte bad_key[sizeof key];
  std::memcpy(bad_key, key, sizeof key);
  bad_key[0] ^= 0x80;
  int type = 0;
  h = reinterpret_cast<HCOSE_MAC0>(COSE_Decode(
      wire.data(), wire.size(), &type, COSE_mac0_object, &err));
  CHECK(h != nullptr);
  if (h == nullptr) return;
  CHECK(type == COSE_mac0_object);
  CHECK(COSE_Mac0_validate(h, key, sizeof key, &err));
  CHECK(!COSE_Mac0_validate(h, bad_key, sizeof bad_key, &err));
  CHECK(COSE_Mac0_Free(h));
}

void test_encrypt0() {
  cose_errback err;
  cn_cbor_errback cberr;
  std::vector<byte> wire;
  HCOSE_ENCRYPT h = COSE_Encrypt_Init(COSE_INIT_FLAGS_NONE, &err);
  CHECK(h != nullptr);
  if (h == nullptr) return;
  CHECK(COSE_Encrypt_SetContent(h, content, content_len, &err));
  CHECK(COSE_Encrypt_map_put_int(
      h, COSE_Header_Algorithm,
      cn_cbor_int_create(COSE_Algorithm_AES_GCM_128, &cberr),
      COSE_PROTECT_ONLY, &err));
  CHECK(COSE_Encrypt_map_put_int(h, COSE_Header_IV,
                                 cn_cbor_data_create(iv, sizeof iv, &cberr),
                                 COSE_UNPROTECT_ONLY, &err));
  CHECK(COSE_Encrypt_encrypt(h, key, 16, &err));
  wire = encode(reinterpret_cast<HCOSE>(h));
  CHECK(wire == encrypt0);
  CHECK(COSE_Encrypt_Free(h));

  int type = 0;
  h = reinterpret_cast<HCOSE_ENCRYPT>(COSE_Decode(
      wire.data(), wire.size(), &type, COSE_encrypt_object, &err));
  CHECK(h != nullptr);
  if (h == nullptr) return;
  CHECK(type == COSE_encrypt_object);
  CHECK(!COSE_Encrypt_decrypt(h, key + 1, 16, &err));
  CHECK(COSE_Encrypt_decrypt(h, key, 16, &err));
  // The length it reports after AES-GCM is 0 (cabin.toml).
  size_t plain_len = 0;
  const byte* plain = COSE_Encrypt_GetContent(h, &plain_len, &err);
  CHECK(plain != nullptr && std::memcmp(plain, content, content_len) == 0);
  CHECK(COSE_Encrypt_Free(h));
}

}  // namespace

int main() {
  test_mac0();
  test_encrypt0();
  if (failures == 0) {
    std::puts("cose-c: COSE_Mac0 and COSE_Encrypt0 round trips passed");
  }
  return failures ? 1 : 0;
}
