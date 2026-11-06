// libtomcrypt's C API from C++11: the register_all_* tables, SHA-512,
// AES-128 in CTR mode (NIST SP 800-38A F.5.1), Poly1305 (RFC 7539
// 2.5.2) and PBKDF2-HMAC-SHA1 (RFC 6070), against published vectors.
#include <cstdio>
#include <cstring>
#include <string>

#include <tomcrypt.h>

namespace {

int failures = 0;

void check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "main.cc: check failed: %s\n", what);
    ++failures;
  }
}

std::string hex(const unsigned char* bytes, unsigned long len) {
  static const char digits[] = "0123456789abcdef";
  std::string out;
  for (unsigned long i = 0; i < len; ++i) {
    out += digits[bytes[i] >> 4];
    out += digits[bytes[i] & 15];
  }
  return out;
}

std::string unhex(const char* text) {
  std::string out;
  for (; text[0] != '\0' && text[1] != '\0'; text += 2) {
    out += static_cast<char>(std::stoi(std::string(text, 2), nullptr, 16));
  }
  return out;
}

const unsigned char* bytes(const std::string& s) {
  return reinterpret_cast<const unsigned char*>(s.data());
}

}  // namespace

int main() {
  check(register_all_ciphers() == CRYPT_OK, "register_all_ciphers");
  check(register_all_hashes() == CRYPT_OK, "register_all_hashes");
  check(register_all_prngs() == CRYPT_OK, "register_all_prngs");
  const int aes = find_cipher("aes");
  const int sha512 = find_hash("sha512");
  const int sha1 = find_hash("sha1");
  check(aes >= 0 && sha512 >= 0 && sha1 >= 0, "find_cipher/find_hash");
  check(find_cipher("twofish") >= 0 && find_hash("whirlpool") >= 0 && find_prng("fortuna") >= 0,
        "other registered algorithms");

  unsigned char out[64];
  unsigned long len = sizeof out;
  check(hash_memory(sha512, reinterpret_cast<const unsigned char*>("abc"), 3, out, &len) ==
                CRYPT_OK &&
            hex(out, len) ==
                "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a"
                "2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f",
        "SHA-512(abc)");

  const std::string key = unhex("2b7e151628aed2a6abf7158809cf4f3c");
  const std::string counter = unhex("f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff");
  const std::string plain =
      unhex("6bc1bee22e409f96e93d7e117393172aae2d8a571e03ac9c9eb76fac45af8e51");
  unsigned char ct[32], back[32];
  symmetric_CTR ctr;
  check(ctr_start(aes, bytes(counter), bytes(key), 16, 0, CTR_COUNTER_BIG_ENDIAN, &ctr) ==
                CRYPT_OK &&
            ctr_encrypt(bytes(plain), ct, 32, &ctr) == CRYPT_OK && ctr_done(&ctr) == CRYPT_OK,
        "CTR encrypt");
  check(hex(ct, 32) == "874d6191b620e3261bef6864990db6ce9806f66b7970fdff8617187bb9fffdff",
        "CTR ciphertext");
  check(ctr_start(aes, bytes(counter), bytes(key), 16, 0, CTR_COUNTER_BIG_ENDIAN, &ctr) ==
                CRYPT_OK &&
            ctr_decrypt(ct, back, 32, &ctr) == CRYPT_OK && ctr_done(&ctr) == CRYPT_OK &&
            std::memcmp(back, plain.data(), 32) == 0,
        "CTR decrypt");

  const std::string poly_key =
      unhex("85d6be7857556d337f4452fe42d506a80103808afb0db2fd4abff6af4149f51b");
  const std::string message = "Cryptographic Forum Research Group";
  len = sizeof out;
  check(poly1305_memory(bytes(poly_key), 32, bytes(message), message.size(), out, &len) ==
                CRYPT_OK &&
            hex(out, len) == "a8061dc1305136c6c22b8baf0c0127a9",
        "Poly1305");

  len = 20;
  check(pkcs_5_alg2(reinterpret_cast<const unsigned char*>("password"), 8,
                    reinterpret_cast<const unsigned char*>("salt"), 4, 4096, sha1, out, &len) ==
                CRYPT_OK &&
            hex(out, len) == "4b007901b765489abead49d926f721d065a429c1",
        "PBKDF2-HMAC-SHA1, 4096 iterations");

  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  return 0;
}
