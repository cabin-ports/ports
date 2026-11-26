// The HS256 token and secret are the sample jwt.io's debugger shows; the
// last test mirrors upstream's README.md:60-95 (issuer "auth0", claim
// "sample").
#include <jwt-cpp/jwt.h>

#include <chrono>
#include <cstdio>
#include <string>

static int failures = 0;

#define CHECK(cond)                                                    \
  do {                                                                 \
    if (!(cond)) {                                                     \
      std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #cond);     \
      ++failures;                                                      \
    }                                                                  \
  } while (0)

int main() {
  const std::string token =
      "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9."
      "eyJzdWIiOiIxMjM0NTY3ODkwIiwibmFtZSI6IkpvaG4gRG9lIiwiaWF0IjoxNTE2MjM5MDIyfQ."
      "SflKxwRJSMeKKF2QT4fwpMeJf36POk6yJV_adQssw5c";

  // Decoding (base64url and JSON).
  auto d = jwt::decode(token);
  CHECK(d.get_algorithm() == "HS256");
  CHECK(d.get_type() == "JWT");
  CHECK(d.get_subject() == "1234567890");
  CHECK(d.get_payload_claim("name").as_string() == "John Doe");
  CHECK(std::chrono::system_clock::to_time_t(d.get_issued_at()) == 1516239022);

  // HMAC verification with the right and a wrong secret (OpenSSL).
  jwt::verify().allow_algorithm(jwt::algorithm::hs256{"your-256-bit-secret"}).verify(d);
  bool rejected = false;
  try {
    jwt::verify().allow_algorithm(jwt::algorithm::hs256{"wrong"}).verify(d);
  } catch (const jwt::error::signature_verification_exception&) {
    rejected = true;
  }
  CHECK(rejected);

  // Sign, then verify and read back.
  auto t = jwt::create()
               .set_issuer("auth0")
               .set_type("JWS")
               .set_payload_claim("sample", jwt::claim(std::string("test")))
               .sign(jwt::algorithm::hs256{"secret"});
  auto r = jwt::decode(t);
  jwt::verify()
      .allow_algorithm(jwt::algorithm::hs256{"secret"})
      .with_issuer("auth0")
      .verify(r);
  CHECK(r.get_payload_claim("sample").as_string() == "test");
  CHECK(r.get_type() == "JWS");

  return failures == 0 ? 0 : 1;
}
