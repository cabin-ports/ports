// Round-trips images through the C++ API: 8-bit RGBA with the encoder's
// automatic colour type, and 16-bit greyscale kept as such through a
// lodepng::State, then checks that garbage is rejected.  C++98, as the probe
// compiles it.
#include <cstddef>
#include <cstdio>
#include <vector>

#include "lodepng.h"

static int fail(const char* what, unsigned error) {
  std::fprintf(stderr, "%s: %u %s\n", what, error, lodepng_error_text(error));
  return 1;
}

int main() {
  const unsigned width = 7, height = 4;

  std::vector<unsigned char> rgba(width * height * 4);
  for (std::size_t i = 0; i < rgba.size(); ++i) {
    rgba[i] = static_cast<unsigned char>(i * 37u + 11u);
  }
  std::vector<unsigned char> png;
  unsigned error = lodepng::encode(png, rgba, width, height);
  if (error) return fail("encode rgba", error);
  std::vector<unsigned char> decoded;
  unsigned w = 0, h = 0;
  error = lodepng::decode(decoded, w, h, png);
  if (error) return fail("decode rgba", error);
  if (w != width || h != height || decoded != rgba) {
    std::fprintf(stderr, "rgba round trip changed the image\n");
    return 1;
  }

  // 16-bit samples are big-endian in raw buffers (lodepng.h).
  std::vector<unsigned char> grey(width * height * 2);
  for (std::size_t i = 0; i < grey.size(); ++i) {
    grey[i] = static_cast<unsigned char>(i * 101u + 7u);
  }
  lodepng::State enc_state;
  enc_state.info_raw.colortype = LCT_GREY;
  enc_state.info_raw.bitdepth = 16;
  enc_state.info_png.color.colortype = LCT_GREY;
  enc_state.info_png.color.bitdepth = 16;
  enc_state.encoder.auto_convert = 0;
  png.clear();
  error = lodepng::encode(png, grey, width, height, enc_state);
  if (error) return fail("encode grey16", error);

  lodepng::State dec_state;
  dec_state.info_raw.colortype = LCT_GREY;
  dec_state.info_raw.bitdepth = 16;
  decoded.clear();
  error = lodepng::decode(decoded, w, h, dec_state, png);
  if (error) return fail("decode grey16", error);
  if (dec_state.info_png.color.colortype != LCT_GREY ||
      dec_state.info_png.color.bitdepth != 16 || decoded != grey) {
    std::fprintf(stderr, "grey16 round trip changed the image\n");
    return 1;
  }

  std::vector<unsigned char> garbage(64, 0x5a);
  decoded.clear();
  if (lodepng::decode(decoded, w, h, garbage) == 0) {
    std::fprintf(stderr, "garbage decoded\n");
    return 1;
  }

  std::printf("lodepng %s: rgba and grey16 round trips ok\n", LODEPNG_VERSION_STRING);
  return 0;
}
