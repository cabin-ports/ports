#include <advobfuscator/string.h>
#include <advobfuscator/aes.h>
#include <advobfuscator/aes_string.h>
#include <advobfuscator/bytes.h>

#include <cstdio>

using namespace andrivet::advobfuscator;

static int failures = 0;

#define CHECK(cond)                                          \
  do {                                                       \
    if (!(cond)) {                                           \
      std::printf("%s:%d: %s\n", __FILE__, __LINE__, #cond); \
      ++failures;                                            \
    }                                                        \
  } while (0)

int main() {
  // Obfuscated string: the literal's algorithms vary with __TIME__, so the stored-bytes check
  // uses explicit parameters (XOR, key 1); access decodes.
  auto x = ObfuscatedString("abcd", {1, KeyAlgorithm::IDENTITY, DataAlgorithm::XOR});
  CHECK(x.raw()[0] != 'a' || x.raw()[1] != 'b' || x.raw()[2] != 'c' || x.raw()[3] != 'd');
  CHECK(x.decode() == "abcd");
  auto s = "abc"_obf;
  CHECK(s[0] == 'a' && s[1] == 'b' && s[2] == 'c' && s[3] == 0);

  static constexpr auto im = "An immutable compile-time string"_obf;
  CHECK(im.decode() == "An immutable compile-time string");

  // Obfuscated bytes.
  static constexpr auto rcon = "01 02 04 08 10 20 40 80 1b 36"_obf_bytes;
  const auto bytes = rcon.decode();
  CHECK(bytes[0] == 0x01 && bytes[5] == 0x20 && bytes[8] == 0x1b && bytes[9] == 0x36);

  // AES-128, FIPS-197 Appendix B.
  static constexpr Block input = {0x32, 0x43, 0xf6, 0xa8, 0x88, 0x5a, 0x30, 0x8d,
                                  0x31, 0x31, 0x98, 0xa2, 0xe0, 0x37, 0x07, 0x34};
  static constexpr Key key = {0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
                              0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c};
  static constexpr Block expected = {0x39, 0x25, 0x84, 0x1d, 0x02, 0xdc, 0x09, 0xfb,
                                     0xdc, 0x11, 0x85, 0x97, 0x19, 0x6a, 0x0b, 0x32};
  const Block encrypted = encrypt(input, key);
  CHECK(encrypted == expected);
  CHECK(decrypt(encrypted, key) == input);

  // AES-CTR string.
  auto secret = "secret"_aes;
  CHECK(secret.data_[0] != 's' || secret.data_[1] != 'e' || secret.data_[2] != 'c');
  CHECK(secret.decrypt() == "secret");

  return failures == 0 ? 0 : 1;
}
