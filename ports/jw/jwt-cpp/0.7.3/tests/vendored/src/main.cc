// The OpenSSL this program compiles against and runs with is the openssl
// port's own 4.1.0, though only jwt-cpp asks for it; the system copies CI
// checks with are 3.x, and a distribution's 4.1.0 names its own directory.
#include <jwt-cpp/jwt.h>

#include <openssl/crypto.h>
#include <openssl/opensslv.h>

#include <cstdio>
#include <cstring>
#include <exception>

int main() {
  auto token = jwt::create()
                   .set_issuer("auth0")
                   .sign(jwt::algorithm::hs256{"secret"});
  bool signed_ok = true;
  try {
    jwt::verify()
        .allow_algorithm(jwt::algorithm::hs256{"secret"})
        .with_issuer("auth0")
        .verify(jwt::decode(token));
  } catch (const std::exception&) {
    signed_ok = false;
  }

  std::printf("OpenSSL headers: %s; library: %s, %s\n", OPENSSL_VERSION_TEXT,
              OpenSSL_version(OPENSSL_VERSION), OpenSSL_version(OPENSSL_DIR));
  if (!signed_ok || OPENSSL_VERSION_MAJOR != 4 || OPENSSL_VERSION_MINOR != 1 ||
      std::strcmp(OpenSSL_version(OPENSSL_VERSION_STRING), "4.1.0") != 0 ||
      std::strcmp(OpenSSL_version(OPENSSL_DIR), "OPENSSLDIR: \"/usr/local/ssl\"") != 0) {
    std::fputs("FAIL: not the openssl port's OpenSSL 4.1.0\n", stderr);
    return 1;
  }
  std::puts("jwt-cpp: built with the openssl port's OpenSSL 4.1.0");
  return 0;
}
