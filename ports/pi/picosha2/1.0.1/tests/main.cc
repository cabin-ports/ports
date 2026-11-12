// Checks PicoSHA2 against known SHA-256 digests, FIPS 180-2's three examples
// among them, through the one-shot hex-string, digest-bytes, incremental and
// input-iterator paths.
#include <picosha2.h>

#include <cstdio>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

namespace {

int failures = 0;

void expect(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "failed: %s\n", what);
    ++failures;
  }
}

const char* const two_blocks_hex =
    "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1";

}  // namespace

int main() {
  expect(picosha2::hash256_hex_string(std::string()) ==
             "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
         "empty");
  expect(picosha2::hash256_hex_string(std::string("abc")) ==
             "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
         "abc");
  // 56 bytes: the length no longer fits the first block's padding.
  const std::string two_blocks =
      "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
  expect(picosha2::hash256_hex_string(two_blocks) == two_blocks_hex,
         "two blocks");

  const std::string fox = "The quick brown fox jumps over the lazy dog";
  std::vector<unsigned char> digest(picosha2::k_digest_size);
  picosha2::hash256(fox, digest);
  expect(digest[0] == 0xd7 && digest[31] == 0x92 &&
             picosha2::bytes_to_hex_string(digest) ==
                 "d7a8fbb307d7809469ca9abcb0082e4f"
                 "8d5651e46d3cdb762d02d0bf37c9e592",
         "digest bytes");

  // One million 'a' fed to the incremental hasher in chunks that straddle
  // block boundaries.
  picosha2::hash256_one_by_one hasher;
  const std::string chunk(999, 'a');
  for (int i = 0; i < 1001; ++i) hasher.process(chunk.begin(), chunk.end());
  hasher.process(chunk.begin(), chunk.begin() + 1);
  hasher.finish();
  expect(picosha2::get_hash_hex_string(hasher) ==
             "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0",
         "one million a");

  // Input iterators are read through a buffer, here of 10 bytes, so the
  // 56-byte message ends in a partial one.
  std::istringstream in(two_blocks);
  std::vector<unsigned char> streamed(picosha2::k_digest_size);
  picosha2::hash256(std::istreambuf_iterator<char>(in),
                    std::istreambuf_iterator<char>(), streamed.begin(),
                    streamed.end(), 10);
  expect(picosha2::bytes_to_hex_string(streamed) == two_blocks_hex,
         "input iterator");

  if (failures != 0) return 1;
  std::puts("picosha2 OK");
  return 0;
}
