// The OpenSSL this program compiles against and runs with is the openssl
// port's own 4.1.0, though only cpp-smtpclient-library asks for it; the
// system copies CI checks with are 3.x, and a distribution's 4.1.0 names its
// own directory.
#include "cpp/forcedsecuresmtpclient.hpp"

#include <openssl/crypto.h>
#include <openssl/opensslv.h>

#include <cstdio>
#include <cstring>

int main() {
  const jed_utils::cpp::ForcedSecureSMTPClient client("smtp.example.com", 465);

  std::printf("OpenSSL headers: %s; library: %s, %s\n", OPENSSL_VERSION_TEXT,
              OpenSSL_version(OPENSSL_VERSION), OpenSSL_version(OPENSSL_DIR));
  if (client.getServerPort() != 465 || OPENSSL_VERSION_MAJOR != 4 ||
      OPENSSL_VERSION_MINOR != 1 ||
      std::strcmp(OpenSSL_version(OPENSSL_VERSION_STRING), "4.1.0") != 0 ||
      std::strcmp(OpenSSL_version(OPENSSL_DIR), "OPENSSLDIR: \"/usr/local/ssl\"") != 0) {
    std::fputs("FAIL: not the openssl port's OpenSSL 4.1.0\n", stderr);
    return 1;
  }
  std::puts("cpp-smtpclient-library: built with the openssl port's OpenSSL 4.1.0");
  return 0;
}
