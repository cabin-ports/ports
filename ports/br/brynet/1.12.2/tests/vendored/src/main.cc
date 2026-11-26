// brynet's TLS build (BRYNET_USE_OPENSSL) against the OpenSSL that the
// openssl port builds under `vendored-openssl`: that it is that one, and that
// SSLHelper loads a certificate and key made here and refuses a missing file.
#include <brynet/net/SSLHelper.hpp>

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/opensslv.h>
#include <openssl/pem.h>
#include <openssl/x509.h>

#include <cstdio>
#include <cstring>
#include <string>

static bool write_self_signed(const std::string &path) {
  EVP_PKEY *key = EVP_EC_gen("P-256");
  X509 *cert = X509_new();
  X509_NAME *name = X509_NAME_new();
  bool ok = key && cert && name && X509_set_version(cert, 2) &&
            ASN1_INTEGER_set(X509_get_serialNumber(cert), 1) &&
            X509_gmtime_adj(X509_getm_notBefore(cert), -60) &&
            X509_gmtime_adj(X509_getm_notAfter(cert), 3600) &&
            X509_set_pubkey(cert, key) &&
            X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
                                       reinterpret_cast<const unsigned char *>("localhost"), -1, -1,
                                       0) &&
            X509_set_subject_name(cert, name) && X509_set_issuer_name(cert, name) &&
            X509_sign(cert, key, EVP_sha256());
  std::FILE *file = ok ? std::fopen(path.c_str(), "wb") : nullptr;
  ok = file && PEM_write_X509(file, cert) == 1 &&
       PEM_write_PrivateKey(file, key, nullptr, nullptr, 0, nullptr, nullptr) == 1;
  if (file) std::fclose(file);
  X509_NAME_free(name);
  X509_free(cert);
  EVP_PKEY_free(key);
  return ok;
}

int main() {
  int failures = 0;
  auto check = [&failures](bool ok, const char *what) {
    if (!ok) {
      std::fprintf(stderr, "FAIL: %s\n", what);
      ++failures;
    }
  };

  check(OPENSSL_VERSION_MAJOR == 4 && OPENSSL_VERSION_MINOR == 1 &&
            std::strcmp(OpenSSL_version(OPENSSL_VERSION_STRING), "4.1.0") == 0,
        "the openssl port's OpenSSL 4.1.0");

  const std::string pem = "brynet-vendored-test.pem";
  check(write_self_signed(pem), "make a certificate and key");

  auto helper = brynet::net::SSLHelper::Create();
  check(!helper->initSSL("", ""), "empty paths are refused");
  check(helper->getOpenSSLCTX() == nullptr, "no context after a refusal");
  check(!helper->initSSL("brynet-no-such-file.pem", "brynet-no-such-file.pem"),
        "a missing file is refused");
  check(helper->initSSL(pem, pem), "certificate and key load");
  check(helper->getOpenSSLCTX() != nullptr, "a context after loading");
  check(!helper->initSSL(pem, pem), "a second init is refused");
  helper->destroySSL();
  check(helper->getOpenSSLCTX() == nullptr, "destroySSL frees the context");

  std::remove(pem.c_str());
  if (failures == 0) std::puts("brynet: TLS against the openssl port's OpenSSL 4.1.0");
  return failures == 0 ? 0 : 1;
}
