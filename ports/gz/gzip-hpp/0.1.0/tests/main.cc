// <cstdint> first: utils.hpp uses uint8_t without including it.
#include <cstdint>

#include <gzip/compress.hpp>
#include <gzip/decompress.hpp>
#include <gzip/utils.hpp>
#include <gzip/version.hpp>

#include <cstdio>
#include <stdexcept>
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

// The runtime_error message of a failed call, or "" when nothing threw.
template <typename F>
std::string error_of(F f) {
  try {
    f();
  } catch (const std::runtime_error &e) {
    return e.what();
  }
  return "";
}

unsigned long le32(const std::string &s, std::size_t at) {
  unsigned long v = 0;
  for (int i = 3; i >= 0; i--) {
    v = (v << 8) | static_cast<unsigned char>(s[at + i]);
  }
  return v;
}

// gzip.compress(b"hello, gzip-hpp\n", mtime=0) and zlib.compress(b"zlib
// stream\n") from Python.
const unsigned char gzip_stream[] = {
    0x1f, 0x8b, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xcb, 0x48,
    0xcd, 0xc9, 0xc9, 0xd7, 0x51, 0x48, 0xaf, 0xca, 0x2c, 0xd0, 0xcd, 0x28,
    0x28, 0xe0, 0x02, 0x00, 0xce, 0x9b, 0x6a, 0x30, 0x10, 0x00, 0x00, 0x00};
const unsigned char zlib_stream[] = {
    0x78, 0x9c, 0xab, 0xca, 0xc9, 0x4c, 0x52, 0x28, 0x2e, 0x29,
    0x4a, 0x4d, 0xcc, 0xe5, 0x02, 0x00, 0x1e, 0xa6, 0x04, 0x68};

}  // namespace

int main() {
  std::string text;
  for (int i = 0; i < 200; i++) {
    text += "hello gzip-hpp ";
  }

  // The zlib.h compiled here is the dependency's, the one linked, not a
  // system copy.
  check(std::string(zlibVersion()) == ZLIB_VERSION,
        "zlib.h matches the linked zlib");

  std::string packed = gzip::compress(text.data(), text.size());
  check(packed.size() > 18 && packed.size() < text.size() / 4,
        "repetitive text compresses");
  check(packed.compare(0, 3, "\x1f\x8b\x08") == 0,
        "the output is a gzip member with deflate");
  check(gzip::is_compressed(packed.data(), packed.size()),
        "is_compressed recognizes gzip");
  if (packed.size() > 18) {
    // The gzip trailer: CRC-32 and length, little-endian, from zlib itself.
    unsigned long crc = crc32(0L, reinterpret_cast<const Bytef *>(text.data()),
                              static_cast<uInt>(text.size()));
    check(le32(packed, packed.size() - 8) == crc, "the trailer's CRC-32");
    check(le32(packed, packed.size() - 4) == text.size(),
          "the trailer's length");
  }
  check(gzip::decompress(packed.data(), packed.size()) == text,
        "compress then decompress round-trips");

  // Level 0 stores the input; a vector works as the output buffer too.
  std::vector<char> stored;
  gzip::Compressor(0).compress(stored, text.data(), text.size());
  check(stored.size() > text.size(), "level 0 stores");
  std::vector<char> restored;
  gzip::Decompressor().decompress(restored, stored.data(), stored.size());
  check(std::string(restored.begin(), restored.end()) == text,
        "stored data decompresses into a vector");

  check(gzip::decompress(reinterpret_cast<const char *>(gzip_stream),
                         sizeof gzip_stream) == "hello, gzip-hpp\n",
        "a gzip stream from Python decompresses");
  check(gzip::decompress(reinterpret_cast<const char *>(zlib_stream),
                         sizeof zlib_stream) == "zlib stream\n",
        "a zlib stream is detected and decompresses");
  check(gzip::is_compressed(reinterpret_cast<const char *>(zlib_stream),
                            sizeof zlib_stream),
        "is_compressed recognizes zlib");
  check(!gzip::is_compressed("plain text", 10), "plain text is not compressed");
  check(!gzip::is_compressed("\x1f\x8b", 2), "two bytes are too short");

  check(error_of([] { gzip::decompress("not gzip data", 13); }) ==
            "incorrect header check",
        "a bad header throws zlib's message");
  // A gzip header, then a final block of the reserved type 3.
  const char bad_block[] = "\x1f\x8b\x08\0\0\0\0\0\0\x03\xff\0\0";
  check(error_of([&] { gzip::decompress(bad_block, sizeof bad_block - 1); }) ==
            "invalid block type",
        "a bad block throws zlib's message");

  std::string over_input = error_of([&] {
    gzip::Compressor(6, 100).compress(stored, text.data(), 101);
  });
  check(over_input.find("more memory than intended") != std::string::npos,
        "Compressor refuses input over its limit");
  std::string over_half = error_of([&] {
    gzip::Decompressor(100).decompress(restored, packed.data(), 51);
  });
  check(over_half.find("more memory than intended") != std::string::npos,
        "Decompressor refuses input over half its limit");
  // 100000 bytes compress to about 130; the output limit stops the bomb.
  std::string bomb = gzip::compress(std::string(100000, 'a').data(), 100000);
  std::string over_output = error_of([&] {
    gzip::Decompressor(1000).decompress(restored, bomb.data(), bomb.size());
  });
  check(bomb.size() < 500 &&
            over_output.find("output string") != std::string::npos,
        "Decompressor refuses output over its limit");

  // version.hpp still says 1.0.0 in the v0.1.0 tag.
  check(std::string(GZIP_VERSION_STRING) == "1.0.0" &&
            GZIP_VERSION_CODE == 10000,
        "version.hpp");

  if (failures != 0) {
    return 1;
  }
  std::printf("gzip-hpp: %zu bytes round-trip through %zu gzip bytes\n",
              text.size(), packed.size());
  return 0;
}
