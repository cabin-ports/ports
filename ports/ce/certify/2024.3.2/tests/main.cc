// Checks Certify without a network: SNI and hostname setup on a TLS stream,
// the verification callback it installs in a TLS context, and the CRLSet
// parser.
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/certify/crlset_parser.hpp>
#include <boost/certify/extensions.hpp>
#include <boost/certify/https_verification.hpp>

#include <openssl/x509_vfy.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace asio = boost::asio;
namespace certify = boost::certify;
namespace ssl = asio::ssl;

namespace {

bool ok = true;

void check(bool pass, const char* what) {
  if (!pass) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ok = false;
  }
}

void check_stream() {
  asio::io_context io;
  ssl::context ctx{ssl::context::tls_client};
  ssl::stream<asio::ip::tcp::socket> stream{io, ctx};

  check(certify::sni_hostname(stream).empty(), "no SNI name initially");
  certify::sni_hostname(stream, "example.org");
  check(certify::sni_hostname(stream) == "example.org", "SNI name round-trips");

  boost::system::error_code ec;
  certify::set_server_hostname(stream, "example.org", ec);
  check(!ec, "set_server_hostname reports success");
  certify::set_server_hostname(stream, "example.com");
}

void check_callback() {
  ssl::context ctx{ssl::context::tls_client};
  ctx.set_verify_mode(ssl::verify_peer);
  check(SSL_CTX_get_verify_callback(ctx.native_handle()) == nullptr,
        "no callback before enabling");
  certify::enable_native_https_server_verification(ctx);
  auto* cb = SSL_CTX_get_verify_callback(ctx.native_handle());
  check(cb != nullptr, "callback installed");
  check(SSL_CTX_get_verify_mode(ctx.native_handle()) == SSL_VERIFY_PEER,
        "verify mode preserved");

  X509_STORE_CTX* store = X509_STORE_CTX_new();
  check(cb(1, store) == 1, "a preverified chain is accepted");
  X509_STORE_CTX_set_error(store, X509_V_ERR_CERT_HAS_EXPIRED);
  check(cb(0, store) == 0, "an expired certificate is rejected");
  X509_STORE_CTX_free(store);
}

void check_crlset() {
  std::vector<std::uint8_t> set = {2, 0, 0xAA, 0xBB};  // header, length 2
  set.insert(set.end(), 32, 7);                         // parent SPKI hash
  const std::uint8_t tail[] = {2, 0, 0, 0,              // two serials
                               3, 'a', 'b', 'c', 2, 'd', 'e'};
  set.insert(set.end(), tail, tail + sizeof tail);

  auto parsed = certify::parse_crlset(asio::buffer(set));
  check(parsed.size() == 1, "one CRLSet entry");
  if (parsed.size() == 1) {
    check(parsed[0].parent_spki_hash[0] == 7 &&
              parsed[0].parent_spki_hash[31] == 7,
          "parent hash");
    check(parsed[0].serials.size() == 2 && parsed[0].serials[0] == "abc" &&
              parsed[0].serials[1] == "de",
          "serials");
  }

  boost::system::error_code ec;
  certify::parse_crlset(asio::buffer(set.data(), 1), ec);
  check(ec == certify::crlset_error::header_length_truncated,
        "truncated header length");
  ec = {};
  certify::parse_crlset(asio::buffer(set.data(), 3), ec);
  check(ec == certify::crlset_error::header_truncated, "truncated header");
  ec = {};
  set[set.size() - 7] = 200;  // first serial claims 200 bytes
  certify::parse_crlset(asio::buffer(set), ec);
  check(ec == certify::crlset_error::serial_truncated, "truncated serial");
  check(ec.message() == "CRLSet serial truncated", "error message");
}

}  // namespace

int main() {
  check_stream();
  check_callback();
  check_crlset();
  if (ok) std::puts("OK");
  return ok ? 0 : 1;
}
