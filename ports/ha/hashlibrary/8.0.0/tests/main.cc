// Known-answer tests: FIPS 180-4 and FIPS 202 example values (SHA-1,
// SHA-256, SHA3), RFC 1321 (MD5), the Keccak-256 empty-input value used by
// Ethereum, the CRC-32 check value for "123456789", and the HMAC examples
// from RFC 2104 / Wikipedia.  Incremental add() must equal the one-shot hash.
#include "crc32.h"
#include "hmac.h"
#include "keccak.h"
#include "md5.h"
#include "sha1.h"
#include "sha256.h"
#include "sha3.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace {

bool ok = true;

void check(const std::string& got, const char* want, const char* what) {
  if (got != want) {
    std::fprintf(stderr, "FAIL: %s\n  got  %s\n  want %s\n", what, got.c_str(), want);
    ok = false;
  }
}

// Feeds `text` through add() in `chunk`-byte pieces.
template <typename H>
std::string streamed(H h, const std::string& text, std::size_t chunk) {
  for (std::size_t i = 0; i < text.size(); i += chunk)
    h.add(text.data() + i, text.size() - i < chunk ? text.size() - i : chunk);
  return h.getHash();
}

// One-shot, then add() in odd-sized chunks; both must give `want`.
template <typename H>
void both(H h, const std::string& text, const char* want, const char* what) {
  check(h(text), want, what);
  h.reset();
  check(streamed(h, text, 7), want, what);
  h.reset();
  check(streamed(h, text, 64), want, what);
}

}  // namespace

int main() {
  const std::string abc = "abc";
  const std::string two = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
  const std::string million(1000000, 'a');

  both(CRC32(), "123456789", "cbf43926", "CRC32 123456789");
  both(CRC32(), abc, "352441c2", "CRC32 abc");

  both(MD5(), "", "d41d8cd98f00b204e9800998ecf8427e", "MD5 empty");
  both(MD5(), abc, "900150983cd24fb0d6963f7d28e17f72", "MD5 abc");
  both(MD5(), "message digest", "f96b697d7cb7938d525a2f31aaf161d0", "MD5 message digest");
  both(MD5(), million, "7707d6ae4e027c70eea2a935c2296f21", "MD5 million a");

  both(SHA1(), "", "da39a3ee5e6b4b0d3255bfef95601890afd80709", "SHA1 empty");
  both(SHA1(), abc, "a9993e364706816aba3e25717850c26c9cd0d89d", "SHA1 abc");
  both(SHA1(), two, "84983e441c3bd26ebaae4aa1f95129e5e54670f1", "SHA1 two-block");
  both(SHA1(), million, "34aa973cd4c4daa4f61eeb2bdbad27316534016f", "SHA1 million a");

  both(SHA256(), "", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "SHA256 empty");
  both(SHA256(), abc, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "SHA256 abc");
  both(SHA256(), two, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1", "SHA256 two-block");
  both(SHA256(), million, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0", "SHA256 million a");

  both(SHA3(SHA3::Bits224), abc, "e642824c3f8cf24ad09234ee7d3c766fc9a3a5168d0c94ad73b46fdf", "SHA3-224 abc");
  both(SHA3(), "", "a7ffc6f8bf1ed76651c14756a061d662f580ff4de43b49fa82d80a4b80f8434a", "SHA3-256 empty");
  both(SHA3(), abc, "3a985da74fe225b2045c172d6bd390bd855f086e3e9d525b46bfe24511431532", "SHA3-256 abc");
  both(SHA3(SHA3::Bits384), abc,
       "ec01498288516fc926459f58e2c6ad8df9b473cb0fc08c2596da7cf0e49be4b298d88cea927ac7f539f1edf228376d25",
       "SHA3-384 abc");
  both(SHA3(SHA3::Bits512), abc,
       "b751850b1a57168a5693cd924b6b096e08f621827444f70d884f5d0240d2712e10e116e9192af3c91a7ec57647e3934057340b4cf408d5a56592f8274eec53f0",
       "SHA3-512 abc");
  both(SHA3(), million, "5c8875ae474a3634ba4fd55ec85bffd661f32aca75c6d699d0cdcb6c115891c1", "SHA3-256 million a");

  both(Keccak(), "", "c5d2460186f7233c927e7db2dcc703c0e500b653ca82273b7bfad8045d85a470", "Keccak-256 empty");
  both(Keccak(), abc, "4e03657aea45a94fc7d47ba826c8d667c0d1e6e33a64a036ec44f58fa12d6c45", "Keccak-256 abc");

  const std::string fox = "The quick brown fox jumps over the lazy dog";
  check(hmac<SHA256>(fox.data(), fox.size(), "key", 3),
        "f7bc83f430538424b13298e6aa6fb143ef4d59a14946175997479dbc2d1a3cd8", "HMAC-SHA256");
  check(hmac<MD5>(fox.data(), fox.size(), "key", 3), "80070713463e7749b90c2dc24911e275", "HMAC-MD5");

  if (ok) std::puts("ok");
  return ok ? 0 : 1;
}
