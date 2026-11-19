// The OpenSSL this program compiles against and runs with is the openssl
// port's own 4.1.0, though only boost-redis asks for it; the system copies CI
// checks with are 3.x, and a distribution's 4.1.0 names its own directory.
#include <boost/asio/io_context.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/redis/connection.hpp>
#include <boost/redis/src.hpp>

#include <openssl/crypto.h>
#include <openssl/opensslv.h>

#include <cstdio>
#include <cstring>
#include <utility>

int main() {
  boost::asio::io_context io;
  boost::asio::ssl::context tls(boost::asio::ssl::context::tls_client);
  const bool context = tls.native_handle() != nullptr;
  boost::redis::connection connection(io, std::move(tls));

  std::printf("OpenSSL headers: %s; library: %s, %s\n", OPENSSL_VERSION_TEXT,
              OpenSSL_version(OPENSSL_VERSION), OpenSSL_version(OPENSSL_DIR));
  if (!context || OPENSSL_VERSION_MAJOR != 4 || OPENSSL_VERSION_MINOR != 1 ||
      std::strcmp(OpenSSL_version(OPENSSL_VERSION_STRING), "4.1.0") != 0 ||
      std::strcmp(OpenSSL_version(OPENSSL_DIR), "OPENSSLDIR: \"/usr/local/ssl\"") != 0) {
    std::fputs("FAIL: not the openssl port's OpenSSL 4.1.0\n", stderr);
    return 1;
  }
  std::puts("boost-redis: built with the openssl port's OpenSSL 4.1.0");
  return 0;
}
