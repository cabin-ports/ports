// The OpenSSL this program compiles against and runs with is the openssl
// port's own 4.1.0, though only cpp-jwt asks for it; the system copies CI
// checks with are 3.x, and a distribution's 4.1.0 names its own directory.
#include <jwt/jwt.hpp>

#include <openssl/crypto.h>
#include <openssl/opensslv.h>

#include <cstdio>
#include <cstring>
#include <string>

int main() {
  using namespace jwt::params;

  jwt::jwt_object obj{algorithm("HS256"), payload({{"some", "payload"}}),
                      secret("secret")};
  bool signed_ok =
      obj.signature() ==
      "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJzb21lIjoicGF5bG9hZCJ9."
      "4twFt5NiznN84AWoo1d7KO1T_yoc0Z6XOpOVswacPZg";

  std::printf("OpenSSL headers: %s; library: %s, %s\n", OPENSSL_VERSION_TEXT,
              OpenSSL_version(OPENSSL_VERSION), OpenSSL_version(OPENSSL_DIR));
  if (!signed_ok || OPENSSL_VERSION_MAJOR != 4 || OPENSSL_VERSION_MINOR != 1 ||
      std::strcmp(OpenSSL_version(OPENSSL_VERSION_STRING), "4.1.0") != 0 ||
      std::strcmp(OpenSSL_version(OPENSSL_DIR), "OPENSSLDIR: \"/usr/local/ssl\"") != 0) {
    std::fputs("FAIL: not the openssl port's OpenSSL 4.1.0\n", stderr);
    return 1;
  }
  std::puts("cpp-jwt: built with the openssl port's OpenSSL 4.1.0");
  return 0;
}
