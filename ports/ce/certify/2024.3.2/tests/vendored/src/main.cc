// The OpenSSL this program compiles against and runs with is the openssl
// port's own 4.1.0, though only certify asks for it; the system copies CI
// checks with are 3.x, and a distribution's 4.1.0 names its own directory.
#include <boost/asio/ssl/context.hpp>
#include <boost/certify/https_verification.hpp>

#include <openssl/crypto.h>
#include <openssl/opensslv.h>

#include <cstdio>
#include <cstring>

int main() {
  boost::asio::ssl::context tls(boost::asio::ssl::context::tls_client);
  boost::certify::enable_native_https_server_verification(tls);
  const bool context = SSL_CTX_get_verify_callback(tls.native_handle()) != nullptr;

  std::printf("OpenSSL headers: %s; library: %s, %s\n", OPENSSL_VERSION_TEXT,
              OpenSSL_version(OPENSSL_VERSION), OpenSSL_version(OPENSSL_DIR));
  if (!context || OPENSSL_VERSION_MAJOR != 4 || OPENSSL_VERSION_MINOR != 1 ||
      std::strcmp(OpenSSL_version(OPENSSL_VERSION_STRING), "4.1.0") != 0 ||
      std::strcmp(OpenSSL_version(OPENSSL_DIR), "OPENSSLDIR: \"/usr/local/ssl\"") != 0) {
    std::fputs("FAIL: not the openssl port's OpenSSL 4.1.0\n", stderr);
    return 1;
  }
  std::puts("certify: built with the openssl port's OpenSSL 4.1.0");
  return 0;
}
