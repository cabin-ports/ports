#include <plusaes/plusaes.hpp>

#include <cstdio>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char *what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

typedef std::vector<unsigned char> Bytes;

Bytes hex(const std::string &s) {
  Bytes out;
  for (std::size_t i = 0; i + 1 < s.size(); i += 2)
    out.push_back(static_cast<unsigned char>(std::stoi(s.substr(i, 2), nullptr, 16)));
  return out;
}

// NIST SP 800-38A F.1.1 / F.2.1 / F.5.1 and FIPS-197 C.1.
const char *kKey = "2b7e151628aed2a6abf7158809cf4f3c";
const char *kPlain =
    "6bc1bee22e409f96e93d7e117393172a" "ae2d8a571e03ac9c9eb76fac45af8e51"
    "30c81c46a35ce411e5fbc1191a0a52ef" "f69f2445df4f9b17ad2b417be66c3710";
const char *kEcb =
    "3ad77bb40d7a3660a89ecaf32466ef97" "f5d3d58503b9699de785895a96fdbaaf"
    "43b1cd7f598ece23881b00e3ed030688" "7b0c785e27e8ad3f8223207104725dd4";
const char *kCbc =
    "7649abac8119b246cee98e9b12e9197d" "5086cb9b507219ee95db113a917678b2"
    "73bed6b8e3c1743b7116e69e22229516" "3ff1caa1681fac09120eca307586e1a7";
const char *kCtr =
    "874d6191b620e3261bef6864990db6ce" "9806f66b7970fdff8617187bb9fffdff"
    "5ae4df3edbd5d35e5b4f09020db03eab" "1e031dda2fbe03d1792170a0f3009cee";

} // namespace

int main() {
  check(plusaes::version() == 0x01000000, "version");

  // FIPS-197 C.1, AES-128 single block.
  {
    const Bytes key = hex("000102030405060708090a0b0c0d0e0f");
    const Bytes in = hex("00112233445566778899aabbccddeeff");
    Bytes out(16);
    check(plusaes::encrypt_ecb(in.data(), 16, key.data(), 16, out.data(), 16, false) ==
              plusaes::kErrorOk,
          "fips197 encrypt status");
    check(out == hex("69c4e0d86a7b0430d8cdb78070b4c55a"), "fips197 ciphertext");
  }

  const Bytes key = hex(kKey), plain = hex(kPlain);

  // ECB, no padding.
  {
    Bytes enc(64), dec(64);
    check(plusaes::encrypt_ecb(plain.data(), 64, key.data(), 16, enc.data(), 64, false) ==
              plusaes::kErrorOk,
          "ecb encrypt status");
    check(enc == hex(kEcb), "ecb ciphertext");
    check(plusaes::decrypt_ecb(enc.data(), 64, key.data(), 16, dec.data(), 64, nullptr) ==
              plusaes::kErrorOk,
          "ecb decrypt status");
    check(dec == plain, "ecb roundtrip");
  }

  // CBC, no padding.
  {
    const unsigned char iv[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    Bytes enc(64), dec(64);
    check(plusaes::encrypt_cbc(plain.data(), 64, key.data(), 16, &iv, enc.data(), 64, false) ==
              plusaes::kErrorOk,
          "cbc encrypt status");
    check(enc == hex(kCbc), "cbc ciphertext");
    check(plusaes::decrypt_cbc(enc.data(), 64, key.data(), 16, &iv, dec.data(), 64, nullptr) ==
              plusaes::kErrorOk,
          "cbc decrypt status");
    check(dec == plain, "cbc roundtrip");
  }

  // CTR.
  {
    const unsigned char nonce[16] = {0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7,
                                     0xf8, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff};
    Bytes buf = plain;
    check(plusaes::crypt_ctr(buf.data(), 64, key.data(), 16, &nonce) == plusaes::kErrorOk,
          "ctr status");
    check(buf == hex(kCtr), "ctr ciphertext");
    plusaes::crypt_ctr(buf.data(), 64, key.data(), 16, &nonce);
    check(buf == plain, "ctr roundtrip");
  }

  // PKCS padding (upstream's own unit-test vector), and its removal.
  {
    const unsigned char k[16 + 1] = "0123456789ABCDEF";
    const Bytes key16(k, k + 16);
    const unsigned char data[] = "1z2y3x";
    check(plusaes::get_padded_encrypted_size(6) == 16, "padded size");
    Bytes enc(16), dec(16);
    check(plusaes::encrypt_ecb(data, 6, key16.data(), 16, enc.data(), 16, true) ==
              plusaes::kErrorOk,
          "padded encrypt status");
    check(enc == hex("aefcf8d0e4ea013d1da23b56193b23aa"), "padded ciphertext");
    unsigned long padding = 0;
    check(plusaes::decrypt_ecb(enc.data(), 16, key16.data(), 16, dec.data(), 16, &padding) ==
              plusaes::kErrorOk,
          "padded decrypt status");
    check(padding == 10 && std::string(dec.begin(), dec.begin() + 6) == "1z2y3x",
          "padded roundtrip");

    // Documented errors.
    check(plusaes::encrypt_ecb(data, 6, key16.data(), 15, enc.data(), 16, true) ==
              plusaes::kErrorInvalidKeySize,
          "invalid key size");
    check(plusaes::encrypt_ecb(data, 6, key16.data(), 16, enc.data(), 8, true) ==
              plusaes::kErrorInvalidBufferSize,
          "invalid buffer size");
    check(plusaes::encrypt_ecb(data, 6, key16.data(), 16, enc.data(), 16, false) ==
              plusaes::kErrorInvalidDataSize,
          "invalid data size");
    // A wrong key yields garbage padding.
    const Bytes wrong = hex(kKey);
    check(plusaes::decrypt_ecb(enc.data(), 16, wrong.data(), 16, dec.data(), 16, &padding) ==
              plusaes::kErrorInvalidKey,
          "bad padding");
  }

  // GCM: McGrew-Viega test case 4 (60-byte plaintext, 20-byte AAD), AES-128.
  {
    const Bytes gkey = hex("feffe9928665731c6d6a8f9467308308");
    const Bytes aad = hex("feedfacedeadbeeffeedfacedeadbeefabaddad2");
    const Bytes pt = hex(
        "d9313225f88406e5a55909c5aff5269a86a7a9531534f7da2e4c303d8a318a72"
        "1c3c0c95956809532fcf0e2449a6b525b16aedf5aa0de657ba637b39");
    const Bytes ct = hex(
        "42831ec2217774244b7221b784d0d49ce3aa212f2c02a4e035c17e2329aca12e"
        "21d514b25466931c7d8f6a5aac84aa051ba30b396a0aac973d58e091");
    const Bytes tag_want = hex("5bc94fbc3221a5db94fae95ae7121a47");
    const unsigned char iv[12] = {0xca, 0xfe, 0xba, 0xbe, 0xfa, 0xce,
                                  0xdb, 0xad, 0xde, 0xca, 0xf8, 0x88};
    unsigned char tag[16] = {};
    Bytes buf = pt;
    check(plusaes::encrypt_gcm(buf.data(), buf.size(), aad.data(), aad.size(), gkey.data(), 16,
                               &iv, &tag) == plusaes::kErrorOk,
          "gcm encrypt status");
    check(buf == ct, "gcm ciphertext");
    check(Bytes(tag, tag + 16) == tag_want, "gcm tag");

    unsigned char bad[16];
    for (int i = 0; i < 16; ++i)
      bad[i] = tag[i];
    bad[0] ^= 1;
    Bytes tampered = buf;
    check(plusaes::decrypt_gcm(tampered.data(), tampered.size(), aad.data(), aad.size(),
                               gkey.data(), 16, &iv, &bad) == plusaes::kErrorInvalidTag,
          "gcm bad tag");
    check(plusaes::decrypt_gcm(buf.data(), buf.size(), aad.data(), aad.size(), gkey.data(), 16,
                               &iv, &tag) == plusaes::kErrorOk,
          "gcm decrypt status");
    check(buf == pt, "gcm roundtrip");
  }

  if (failures == 0)
    std::puts("plusaes OK");
  return failures == 0 ? 0 : 1;
}
