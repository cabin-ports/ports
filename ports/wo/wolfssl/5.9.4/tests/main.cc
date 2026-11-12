// Drives wolfCrypt from C++: SHA-512 and HKDF known answers, RSA PKCS #1
// v1.5 and PSS signatures with the 2048-bit test key in
// <wolfssl/certs_test.h>, which a tampered message must fail, and the
// library's error strings.
#define USE_CERT_BUFFERS_2048

#include <wolfssl/options.h>
#include <wolfssl/ssl.h>
#include <wolfssl/certs_test.h>
#include <wolfssl/wolfcrypt/error-crypt.h>
#include <wolfssl/wolfcrypt/hash.h>
#include <wolfssl/wolfcrypt/hmac.h>
#include <wolfssl/wolfcrypt/random.h>
#include <wolfssl/wolfcrypt/rsa.h>
#include <wolfssl/wolfcrypt/sha512.h>

#include <array>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, int line, const char *what)
{
  if (!ok) {
    std::fprintf(stderr, "main.cc:%d: check failed: %s\n", line, what);
    ++failures;
  }
}

#define CHECK(expr) check((expr), __LINE__, #expr)

std::string hex(const byte *data, std::size_t len)
{
  static const char digits[] = "0123456789abcdef";
  std::string out;
  for (std::size_t i = 0; i < len; ++i) {
    out += digits[data[i] >> 4];
    out += digits[data[i] & 15];
  }
  return out;
}

void sha512()
{
  std::array<byte, WC_SHA512_DIGEST_SIZE> digest{};
  wc_Sha512 sha;
  CHECK(wc_InitSha512(&sha) == 0);
  CHECK(wc_Sha512Update(&sha, reinterpret_cast<const byte *>("abc"), 3) == 0);
  CHECK(wc_Sha512Final(&sha, digest.data()) == 0);
  wc_Sha512Free(&sha);
  CHECK(hex(digest.data(), digest.size()) ==
        "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a"
        "2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f");
}

void hkdf()
{
  // RFC 5869, test case 1.
  std::vector<byte> ikm(22, 0x0b);
  const byte salt[] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c};
  const byte info[] = {0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9};
  std::array<byte, 42> okm{};
  CHECK(wc_HKDF(WC_SHA256, ikm.data(), static_cast<word32>(ikm.size()), salt, sizeof salt, info,
                sizeof info, okm.data(), static_cast<word32>(okm.size())) == 0);
  CHECK(hex(okm.data(), okm.size()) ==
        "3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf34007208d5b887185865");
}

void rsa()
{
  WC_RNG rng;
  RsaKey key;
  word32 idx = 0;
  CHECK(wc_InitRng(&rng) == 0);
  CHECK(wc_InitRsaKey(&key, nullptr) == 0);
  CHECK(wc_RsaPrivateKeyDecode(server_key_der_2048, &idx, &key, sizeof_server_key_der_2048) == 0);
  CHECK(wc_RsaEncryptSize(&key) == 256);
  CHECK(wc_RsaSetRNG(&key, &rng) == 0);

  std::array<byte, WC_SHA256_DIGEST_SIZE> digest{};
  const char message[] = "signed by wolfCrypt";
  CHECK(wc_Sha256Hash(reinterpret_cast<const byte *>(message), sizeof message - 1, digest.data()) == 0);

  std::vector<byte> sig(256), out(256);
  int len = wc_RsaSSL_Sign(digest.data(), static_cast<word32>(digest.size()), sig.data(),
                           static_cast<word32>(sig.size()), &key, &rng);
  CHECK(len == 256);
  CHECK(wc_RsaSSL_Verify(sig.data(), 256, out.data(), static_cast<word32>(out.size()), &key) ==
            static_cast<int>(digest.size()) &&
        std::memcmp(out.data(), digest.data(), digest.size()) == 0);
  sig[128] ^= 1;
  CHECK(wc_RsaSSL_Verify(sig.data(), 256, out.data(), static_cast<word32>(out.size()), &key) < 0);

  len = wc_RsaPSS_Sign(digest.data(), static_cast<word32>(digest.size()), sig.data(),
                       static_cast<word32>(sig.size()), WC_HASH_TYPE_SHA256, WC_MGF1SHA256, &key, &rng);
  CHECK(len == 256);
  int verified = wc_RsaPSS_Verify(sig.data(), 256, out.data(), static_cast<word32>(out.size()),
                                  WC_HASH_TYPE_SHA256, WC_MGF1SHA256, &key);
  CHECK(verified > 0 && wc_RsaPSS_CheckPadding(digest.data(), static_cast<word32>(digest.size()), out.data(),
                                               static_cast<word32>(verified), WC_HASH_TYPE_SHA256) == 0);
  digest[0] ^= 1;
  CHECK(wc_RsaPSS_CheckPadding(digest.data(), static_cast<word32>(digest.size()), out.data(),
                               static_cast<word32>(verified), WC_HASH_TYPE_SHA256) == BAD_PADDING_E);

  wc_FreeRsaKey(&key);
  wc_FreeRng(&rng);
}

}  // namespace

int main()
{
  CHECK(wolfSSL_Init() == WOLFSSL_SUCCESS);
  CHECK(std::string(wolfSSL_lib_version()) == LIBWOLFSSL_VERSION_STRING);
  CHECK(LIBWOLFSSL_VERSION_HEX == 0x05009004);
  sha512();
  hkdf();
  rsa();
  CHECK(std::string(wc_GetErrorString(BAD_FUNC_ARG)) == "Bad function argument");
  CHECK(wolfSSL_Cleanup() == WOLFSSL_SUCCESS);

  std::printf("wolfSSL C++: %s\n", failures == 0 ? "ok" : "FAILED");
  return failures == 0 ? 0 : 1;
}
