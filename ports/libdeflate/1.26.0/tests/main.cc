// The C++ consumer: the C API through libdeflate.h's extern "C" block,
// with RAII owners, a gzip round trip and a stream written by zlib.
#include <libdeflate.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#define CHECK(cond)                                                    \
  do {                                                                 \
    if (!(cond)) {                                                     \
      std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__,      \
                   __LINE__, #cond);                                   \
      std::exit(1);                                                    \
    }                                                                  \
  } while (0)

struct FreeCompressor {
  void operator()(libdeflate_compressor* c) const {
    libdeflate_free_compressor(c);
  }
};
struct FreeDecompressor {
  void operator()(libdeflate_decompressor* d) const {
    libdeflate_free_decompressor(d);
  }
};

int main() {
  std::string text;
  for (int i = 0; i < 2000; ++i) {
    text += "line " + std::to_string(i % 37) + ": libdeflate from C++\n";
  }

  std::unique_ptr<libdeflate_compressor, FreeCompressor> c(
      libdeflate_alloc_compressor(9));
  std::unique_ptr<libdeflate_decompressor, FreeDecompressor> d(
      libdeflate_alloc_decompressor());
  CHECK(c && d);

  std::vector<unsigned char> packed(
      libdeflate_gzip_compress_bound(c.get(), text.size()));
  const std::size_t n = libdeflate_gzip_compress(
      c.get(), text.data(), text.size(), packed.data(), packed.size());
  CHECK(n > 0 && n < text.size() / 4);

  std::string back(text.size(), '\0');
  std::size_t got = 0;
  CHECK(libdeflate_gzip_decompress(d.get(), packed.data(), n, &back[0],
                                   back.size(), &got) == LIBDEFLATE_SUCCESS);
  CHECK(got == text.size() && back == text);
  // The gzip trailer carries, little-endian, the CRC-32 the checksum API
  // computes.
  const std::uint32_t trailer = std::uint32_t(packed[n - 8]) |
                                std::uint32_t(packed[n - 7]) << 8 |
                                std::uint32_t(packed[n - 6]) << 16 |
                                std::uint32_t(packed[n - 5]) << 24;
  CHECK(trailer == libdeflate_crc32(0, text.data(), text.size()));

  // Python's zlib.compress(b"hello, hello, hello", 9).
  static const unsigned char zlib_stream[] = {
      0x78, 0xda, 0xcb, 0x48, 0xcd, 0xc9, 0xc9, 0xd7, 0x51, 0xc8,
      0x40, 0xa2, 0x00, 0x44, 0x28, 0x06, 0xd5};
  char buf[19];
  CHECK(libdeflate_zlib_decompress(d.get(), zlib_stream, sizeof zlib_stream,
                                   buf, sizeof buf,
                                   nullptr) == LIBDEFLATE_SUCCESS);
  CHECK(std::string(buf, sizeof buf) == "hello, hello, hello");

  CHECK(libdeflate_alloc_compressor(13) == nullptr);
  std::puts("ok");
  return 0;
}
