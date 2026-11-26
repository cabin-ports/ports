// The token is README.md:91's HS256 output for payload {"some":"payload"}
// and secret "secret" (also PyJWT's, README.md:58).
#include <jwt/jwt.hpp>

#include <cstdio>
#include <string>
#include <system_error>

static int failures = 0;

#define CHECK(cond)                                                \
  do {                                                             \
    if (!(cond)) {                                                 \
      std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #cond); \
      ++failures;                                                  \
    }                                                              \
  } while (0)

int main() {
  using namespace jwt::params;

  const std::string expected =
      "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9."
      "eyJzb21lIjoicGF5bG9hZCJ9."
      "4twFt5NiznN84AWoo1d7KO1T_yoc0Z6XOpOVswacPZg";

  // Encode (HMAC through OpenSSL).
  jwt::jwt_object obj{algorithm("HS256"), payload({{"some", "payload"}}),
                      secret("secret")};
  CHECK(obj.signature() == expected);

  // Decode and verify with the right key; read a claim back.
  auto dec = jwt::decode(expected, algorithms({"HS256"}), secret("secret"));
  CHECK(dec.payload().get_claim_value<std::string>("some") == "payload");
  CHECK(dec.header().algo() == jwt::algorithm::HS256);

  // A wrong key is rejected, by exception and by error code.
  bool rejected = false;
  try {
    jwt::decode(expected, algorithms({"HS256"}), secret("wrong"));
  } catch (const jwt::InvalidSignatureError&) {
    rejected = true;
  }
  CHECK(rejected);

  std::error_code ec;
  jwt::decode(expected, algorithms({"HS256"}), ec, secret("wrong"));
  CHECK(ec.value() == static_cast<int>(jwt::VerificationErrc::InvalidSignature));

  // A claim added with add_claim survives a round trip.
  jwt::jwt_object obj2{algorithm("HS256"), secret("k")};
  obj2.add_claim("iss", "issuer").add_claim("id", 42);
  auto dec2 = jwt::decode(obj2.signature(), algorithms({"HS256"}), secret("k"),
                          issuer("issuer"));
  CHECK(dec2.payload().get_claim_value<int>("id") == 42);

  return failures == 0 ? 0 : 1;
}
