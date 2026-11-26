#include <mgs/base16.hpp>
#include <mgs/base32.hpp>
#include <mgs/base32hex.hpp>
#include <mgs/base64.hpp>
#include <mgs/base64url.hpp>

#include <cstdio>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char *what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    failures++;
  }
}

// RFC 4648 section 10 test vectors.
const char *const plain[] = {"", "f", "fo", "foo", "foob", "fooba", "foobar"};

template <typename Codec>
void check_vectors(const char *name, const char *const (&encoded)[7]) {
  for (int i = 0; i < 7; i++) {
    const std::string enc = Codec::encode(std::string(plain[i]));
    check(enc == encoded[i], name);
    const std::vector<unsigned char> dec = Codec::decode(std::string(encoded[i]));
    check(std::string(dec.begin(), dec.end()) == plain[i], name);
  }
}

// Decoding must throw E; any other outcome fails.
template <typename E, typename Codec>
void check_throws(const char *what, const std::string &input) {
  try {
    Codec::decode(input);
  } catch (const E &) {
    return;
  } catch (...) {
  }
  check(false, what);
}

} // namespace

int main() {
  const char *const b64[] = {"", "Zg==", "Zm8=", "Zm9v", "Zm9vYg==", "Zm9vYmE=", "Zm9vYmFy"};
  const char *const b32[] = {"", "MY======", "MZXQ====", "MZXW6===", "MZXW6YQ=", "MZXW6YTB", "MZXW6YTBOI======"};
  const char *const b32hex[] = {"", "CO======", "CPNG====", "CPNMU===", "CPNMUOG=", "CPNMUOJ1", "CPNMUOJ1E8======"};
  const char *const b16[] = {"", "66", "666F", "666F6F", "666F6F62", "666F6F6261", "666F6F626172"};
  check_vectors<mgs::base64>("base64 vectors", b64);
  check_vectors<mgs::base32>("base32 vectors", b32);
  check_vectors<mgs::base32hex>("base32hex vectors", b32hex);
  check_vectors<mgs::base16>("base16 vectors", b16);

  // Bytes 0xfb 0xff differ between the two base64 alphabets.
  const std::string bytes = "\xfb\xff";
  check(mgs::base64::encode(bytes) == "+/8=", "base64 alphabet");
  check(mgs::base64url::encode(bytes) == "-_8=", "base64url alphabet");

  check_throws<mgs::exceptions::invalid_input_error, mgs::base64>("base64 invalid char", "Zm9v!mFy");
  check_throws<mgs::exceptions::invalid_input_error, mgs::base16>("base16 invalid char", "66G6");
  check_throws<mgs::exceptions::unexpected_eof_error, mgs::base64>("base64 truncated", "Zm9vY");

  if (failures == 0)
    std::puts("ok");
  return failures != 0;
}
