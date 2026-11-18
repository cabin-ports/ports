// From C++: a SHA-256 known answer fed in two parts through libcrypto, and
// a TLS context from libssl that takes a protocol floor.
#include <openssl/evp.h>
#include <openssl/ssl.h>

#include <cstdio>
#include <cstring>

namespace {

struct Digest {
  EVP_MD_CTX *context;
  Digest() : context(EVP_MD_CTX_new()) {}
  ~Digest() { EVP_MD_CTX_free(context); }
};

bool sha256_answers() {
  static const unsigned char expected[32] = {
      0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
      0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c, 0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad};
  unsigned char digest[EVP_MAX_MD_SIZE];
  unsigned int length = 0;
  Digest sha256;
  return sha256.context != NULL && EVP_DigestInit_ex(sha256.context, EVP_sha256(), NULL) == 1 &&
         EVP_DigestUpdate(sha256.context, "a", 1) == 1 && EVP_DigestUpdate(sha256.context, "bc", 2) == 1 &&
         EVP_DigestFinal_ex(sha256.context, digest, &length) == 1 && length == 32 &&
         std::memcmp(digest, expected, 32) == 0;
}

bool a_context_takes_a_floor() {
  SSL_CTX *context = SSL_CTX_new(TLS_method());
  const bool ok = context != NULL && SSL_CTX_set_min_proto_version(context, TLS1_3_VERSION) == 1 &&
                  SSL_CTX_get_min_proto_version(context) == TLS1_3_VERSION;
  SSL_CTX_free(context);
  return ok;
}

}  // namespace

int main() {
  if (!sha256_answers()) {
    std::fputs("SHA-256 of \"abc\" is wrong\n", stderr);
    return 1;
  }
  if (!a_context_takes_a_floor()) {
    std::fputs("no TLS context with a protocol floor\n", stderr);
    return 2;
  }
  return 0;
}
