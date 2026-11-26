// Round trips 4x4 blocks through the BC1, BC3 and BC4 codecs.  Expected
// values come from the formats: BC1 endpoints are RGB565 expanded to 8 bits
// ((v << 3) | (v >> 2) for 5 bits, (v << 2) | (v >> 4) for 6), and BC4
// endpoints are 8-bit values, so a block of one or two values is exact.
#define RGBCX_IMPLEMENTATION
#include "rgbcx.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace {

bool ok = true;

void check(bool pass, const char* what) {
  if (!pass) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ok = false;
  }
}

struct Rgba {
  uint8_t r, g, b, a;
};

// Every pixel of the block equals c exactly.
bool all_equal(const uint8_t* px, Rgba c) {
  for (int i = 0; i < 16; ++i) {
    if (px[i * 4] != c.r || px[i * 4 + 1] != c.g || px[i * 4 + 2] != c.b || px[i * 4 + 3] != c.a)
      return false;
  }
  return true;
}

void fill(uint8_t* px, Rgba c) {
  for (int i = 0; i < 16; ++i) {
    px[i * 4] = c.r;
    px[i * 4 + 1] = c.g;
    px[i * 4 + 2] = c.b;
    px[i * 4 + 3] = c.a;
  }
}

// Encodes a solid block at level 10 and decodes it back.
bool bc1_solid_exact(Rgba c) {
  uint8_t src[64], blk[8], out[64];
  fill(src, c);
  rgbcx::encode_bc1(10, blk, src, true, false);
  rgbcx::unpack_bc1(blk, out);
  return all_equal(out, Rgba{c.r, c.g, c.b, 255});
}

int abs_diff(int a, int b) { return a > b ? a - b : b - a; }

// Within the RGB565 quantisation step of the nearest representable value:
// a 5-bit channel (step 8 or 9 after expansion) is off by at most 4, the
// 6-bit green channel (step 4) by at most 2.
bool near565(const uint8_t* p, Rgba c) {
  return abs_diff(p[0], c.r) <= 4 && abs_diff(p[1], c.g) <= 2 && abs_diff(p[2], c.b) <= 4 &&
         p[3] == 255;
}

}  // namespace

int main() {
  rgbcx::init();

  // BC1: colours that RGB565 holds exactly decode exactly.
  check(bc1_solid_exact(Rgba{255, 0, 0, 255}), "bc1 solid red is exact");
  check(bc1_solid_exact(Rgba{0, 0, 0, 255}), "bc1 solid black is exact");
  check(bc1_solid_exact(Rgba{0, 255, 0, 255}), "bc1 solid green is exact");

  // BC1: a hand-built block (c0 = 0xF800 little-endian, c1 = 0, selectors 0)
  // decodes to red, independent of the encoder.
  {
    const uint8_t blk[8] = {0x00, 0xF8, 0x00, 0x00, 0, 0, 0, 0};
    uint8_t out[64];
    rgbcx::unpack_bc1(blk, out);
    check(all_equal(out, Rgba{255, 0, 0, 255}), "bc1 hand-built red block decodes to red");
  }

  // BC1: two colours; each decoded pixel is within quantisation of the
  // source colour it came from.
  {
    const Rgba a{200, 100, 50, 255}, b{20, 150, 220, 255};
    uint8_t src[64], blk[8], out[64];
    for (int i = 0; i < 16; ++i) {
      const Rgba c = (i % 2) ? b : a;
      src[i * 4] = c.r;
      src[i * 4 + 1] = c.g;
      src[i * 4 + 2] = c.b;
      src[i * 4 + 3] = 255;
    }
    rgbcx::encode_bc1(10, blk, src, true, false);
    rgbcx::unpack_bc1(blk, out);
    bool pass = true;
    for (int i = 0; i < 16; ++i) pass = pass && near565(out + i * 4, (i % 2) ? b : a);
    check(pass, "bc1 two-colour block stays within RGB565 quantisation");
  }

  // BC4: a constant value, and two values (both are endpoints, so exact).
  {
    uint8_t src[64], blk[8], out[64];
    for (int i = 0; i < 16; ++i) src[i * 4] = 77;
    rgbcx::encode_bc4(blk, src);
    rgbcx::unpack_bc4(blk, out);
    bool pass = true;
    for (int i = 0; i < 16; ++i) pass = pass && out[i * 4] == 77;
    check(pass, "bc4 constant block is exact");

    for (int i = 0; i < 16; ++i) src[i * 4] = (i % 2) ? 200 : 10;
    rgbcx::encode_bc4(blk, src);
    rgbcx::unpack_bc4(blk, out);
    pass = true;
    for (int i = 0; i < 16; ++i) pass = pass && out[i * 4] == ((i % 2) ? 200 : 10);
    check(pass, "bc4 two-value block is exact");
  }

  // BC4: a hand-built block (r0 = r1 = 77, selectors 0) decodes to 77.
  {
    const uint8_t blk[8] = {77, 77, 0, 0, 0, 0, 0, 0};
    uint8_t out[16];
    rgbcx::unpack_bc4(blk, out, 1);
    bool pass = true;
    for (int i = 0; i < 16; ++i) pass = pass && out[i] == 77;
    check(pass, "bc4 hand-built block decodes to its endpoint");
  }

  // BC4: r0 > r1 selects the 8-value ramp; selectors 0..7 in pixels 0..7.
  {
    const uint8_t blk[8] = {200, 10, 0x88, 0xC6, 0xFA, 0, 0, 0};
    const uint8_t want[8] = {200, 10, 172, 145, 118, 91, 64, 37};
    uint8_t out[16];
    rgbcx::unpack_bc4(blk, out, 1);
    bool pass = true;
    for (int i = 0; i < 8; ++i) pass = pass && out[i] == want[i];
    for (int i = 8; i < 16; ++i) pass = pass && out[i] == 200;
    check(pass, "bc4 hand-built block decodes its interpolated values");
  }

  // BC1: c0 = red > c1 = blue selects 4-colour mode; selectors 0..3 in row 0.
  {
    const uint8_t blk[8] = {0x00, 0xF8, 0x1F, 0x00, 0xE4, 0, 0, 0};
    const Rgba want[4] = {{255, 0, 0, 255}, {0, 0, 255, 255}, {170, 0, 85, 255}, {85, 0, 170, 255}};
    uint8_t out[64];
    rgbcx::unpack_bc1(blk, out);
    bool pass = true;
    for (int i = 0; i < 4; ++i)
      pass = pass && out[i * 4] == want[i].r && out[i * 4 + 1] == want[i].g &&
             out[i * 4 + 2] == want[i].b && out[i * 4 + 3] == want[i].a;
    check(pass, "bc1 hand-built block decodes its interpolated colours");
  }

  // BC3: an exact colour with a two-value alpha pattern round trips.
  {
    uint8_t src[64], blk[16], out[64];
    for (int i = 0; i < 16; ++i) {
      src[i * 4] = 255;
      src[i * 4 + 1] = 0;
      src[i * 4 + 2] = 0;
      src[i * 4 + 3] = (i % 2) ? 220 : 40;
    }
    rgbcx::encode_bc3(10, blk, src);
    rgbcx::unpack_bc3(blk, out);
    bool pass = true;
    for (int i = 0; i < 16; ++i) {
      pass = pass && out[i * 4] == 255 && out[i * 4 + 1] == 0 && out[i * 4 + 2] == 0 &&
             out[i * 4 + 3] == ((i % 2) ? 220 : 40);
    }
    check(pass, "bc3 colour and alpha round trip");
  }

  if (ok) std::puts("rgbcx: all checks passed");
  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
