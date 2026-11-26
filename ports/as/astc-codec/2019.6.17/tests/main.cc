#include <astc-codec/astc-codec.h>

#include <cstdint>
#include <cstdio>
#include <vector>

using astc_codec::ASTCDecompressToRGBA;
using astc_codec::FootprintType;

static int failures = 0;

static void check(bool ok, const char* what) {
  if (!ok) {
    std::printf("FAIL: %s\n", what);
    ++failures;
  }
}

int main() {
  // Upstream's testdata/checkerboard.astc: four 8x8 blocks of a 16x16 image
  // whose pixel (x, y) is opaque white when x + y is even, else opaque black.
  const uint8_t block[16] = {0x44, 0x05, 0x00, 0xfe, 0x01, 0x00, 0x00, 0x00,
                             0x55, 0xaa, 0x55, 0xaa, 0x55, 0xaa, 0x55, 0xaa};
  std::vector<uint8_t> data;
  for (int i = 0; i < 4; ++i) data.insert(data.end(), block, block + 16);

  std::vector<uint8_t> out(16 * 16 * 4);
  check(ASTCDecompressToRGBA(data.data(), data.size(), 16, 16,
                             FootprintType::k8x8, out.data(), out.size(), 64),
        "decode 16x16");
  bool pattern = true;
  for (int y = 0; y < 16; ++y) {
    for (int x = 0; x < 16; ++x) {
      const uint8_t v = ((x + y) % 2 == 0) ? 0xff : 0x00;
      const uint8_t* p = &out[y * 64 + x * 4];
      pattern = pattern && p[0] == v && p[1] == v && p[2] == v && p[3] == 0xff;
    }
  }
  check(pattern, "checkerboard pixels");

  // A 13x11 image still needs all four blocks; only the visible part is written.
  std::vector<uint8_t> crop(13 * 11 * 4);
  check(ASTCDecompressToRGBA(data.data(), data.size(), 13, 11,
                             FootprintType::k8x8, crop.data(), crop.size(), 52),
        "decode 13x11");
  check(crop[(10 * 13 + 12) * 4] == 0xff && crop[(10 * 13 + 11) * 4] == 0x00,
        "cropped corner");

  // A void-extent block: one constant RGBA color (16 bits per channel).
  const uint8_t solid[16] = {0xfc, 0xfd, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                             0xff, 0xff, 0x00, 0x00, 0x00, 0x80, 0xff, 0xff};
  uint8_t px[4 * 4 * 4];
  check(ASTCDecompressToRGBA(solid, 16, 4, 4, FootprintType::k4x4, px,
                             sizeof(px), 16),
        "decode void-extent");
  bool constant = true;
  for (int i = 0; i < 16; ++i) {
    constant = constant && px[i * 4] == 255 && px[i * 4 + 1] == 0 &&
               px[i * 4 + 2] == 127 && px[i * 4 + 3] == 255;
  }
  check(constant, "void-extent color");

  // Rejected inputs.
  check(!ASTCDecompressToRGBA(data.data(), 48, 16, 16, FootprintType::k8x8,
                              out.data(), out.size(), 64),
        "too few blocks");
  check(!ASTCDecompressToRGBA(data.data(), data.size(), 0, 16,
                              FootprintType::k8x8, out.data(), out.size(), 64),
        "zero width");
  check(!ASTCDecompressToRGBA(data.data(), data.size(), 16, 16,
                              FootprintType::k8x8, out.data(), out.size(), 32),
        "stride too small");
  check(!ASTCDecompressToRGBA(data.data(), data.size(), 16, 16,
                              FootprintType::kCount, out.data(), out.size(),
                              64),
        "invalid footprint");

  return failures == 0 ? 0 : 1;
}
