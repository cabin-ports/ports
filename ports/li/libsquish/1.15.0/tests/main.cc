// Hand-built DXT blocks decode to the values colourblock.cpp and alpha.cpp
// define, and compression reproduces inputs the formats represent
// exactly.  C++98, the port's interface standard.
#include <squish.h>

#include <cstdio>
#include <cstring>

typedef squish::u8 u8;

static int failures = 0;

static void check(bool ok, char const* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

static bool is(u8 const* p, int r, int g, int b, int a) {
  return p[0] == r && p[1] == g && p[2] == b && p[3] == a;
}

static void set(u8* p, int r, int g, int b, int a) {
  p[0] = (u8)r;
  p[1] = (u8)g;
  p[2] = (u8)b;
  p[3] = (u8)a;
}

static void fill(u8* rgba, int r, int g, int b, int a) {
  for (int i = 0; i < 16; ++i) set(rgba + 4 * i, r, g, b, a);
}

// A block of the four colours a yellow/blue DXT1 block decodes to, one
// column each: colour fits must find the same endpoints and indices.
// (Not red/blue: the fits' colour axis search starts from grey and
// can fail on a line orthogonal to it; see cabin.toml.)
static void palette_block(u8* rgba) {
  static int const colours[4][3] = {
      {255, 255, 0}, {0, 0, 255}, {170, 170, 85}, {85, 85, 170}};
  for (int i = 0; i < 16; ++i) {
    int const* c = colours[i % 4];
    set(rgba + 4 * i, c[0], c[1], c[2], 255);
  }
}

static bool same_pixels(u8 const* a, u8 const* b) {
  return std::memcmp(a, b, 64) == 0;
}

static void decode_blocks() {
  // c0 = 0xF800 (red) > c1 = 0x001F (blue): four colours, the midpoints
  // (2*c0 + c1)/3 and (c0 + 2*c1)/3; every row holds indices 0, 1, 2, 3.
  u8 const four[8] = {0x00, 0xF8, 0x1F, 0x00, 0xE4, 0xE4, 0xE4, 0xE4};
  u8 rgba[64];
  squish::Decompress(rgba, four, squish::kDxt1);
  bool ok = true;
  for (int y = 0; y < 4; ++y) {
    u8 const* row = rgba + 16 * y;
    ok = ok && is(row, 255, 0, 0, 255) && is(row + 4, 0, 0, 255, 255) &&
         is(row + 8, 170, 0, 85, 255) && is(row + 12, 85, 0, 170, 255);
  }
  check(ok, "DXT1 four-colour block");

  u8 again[64];
  squish::Decompress(again, four, 0);
  check(same_pixels(rgba, again), "flags 0 decode DXT1");

  // c0 <= c1: DXT1's three colours, the average, and transparent black.
  u8 const three[8] = {0x1F, 0x00, 0x00, 0xF8, 0xE4, 0xE4, 0xE4, 0xE4};
  squish::Decompress(rgba, three, squish::kDxt1);
  check(is(rgba, 0, 0, 255, 255) && is(rgba + 4, 255, 0, 0, 255) &&
            is(rgba + 8, 127, 0, 127, 255) && is(rgba + 12, 0, 0, 0, 0),
        "DXT1 three-colour block");

  // The same colour bytes after a DXT3 alpha block always use four
  // colours; alpha nibble n decodes to 17 * n.
  u8 dxt3[16] = {0x10, 0x32, 0x54, 0x76, 0x98, 0xBA, 0xDC, 0xFE};
  std::memcpy(dxt3 + 8, three, 8);
  squish::Decompress(rgba, dxt3, squish::kDxt3);
  check(is(rgba, 0, 0, 255, 0) && is(rgba + 4, 255, 0, 0, 17) &&
            is(rgba + 8, 85, 0, 170, 34) && is(rgba + 12, 170, 0, 85, 51) &&
            rgba[63] == 255,
        "DXT3 block");

  // DXT5 alpha: a0 = 200 > a1 = 40 gives six interpolants, index i in
  // 2..7 decoding to ((8 - i)*a0 + (i - 1)*a1)/7.  The 3-bit indices of
  // pixels 0-4, low bits first: 0, 1, 2, 3, 7 = 0x7688.
  u8 dxt5[16] = {200, 40, 0x88, 0x76, 0x00, 0x00, 0x00, 0x00};
  std::memcpy(dxt5 + 8, four, 8);
  squish::Decompress(rgba, dxt5, squish::kDxt5);
  int const want[5] = {200, 40, (6 * 200 + 1 * 40) / 7, (5 * 200 + 2 * 40) / 7,
                       (1 * 200 + 6 * 40) / 7};
  bool alpha_ok = true;
  for (int i = 0; i < 5; ++i)
    alpha_ok = alpha_ok && rgba[4 * i + 3] == want[i];
  check(alpha_ok && is(rgba, 255, 0, 0, 200), "DXT5 block");
}

static void single_colours() {
  // Colours whose 5:6:5 quantization is exact come back unchanged.
  static int const colours[][3] = {{255, 0, 0},     {0, 255, 0},
                                   {0, 0, 255},     {255, 255, 255},
                                   {0, 0, 0},       {132, 130, 66}};
  static int const methods[] = {squish::kDxt1, squish::kDxt3, squish::kDxt5};
  for (unsigned c = 0; c < sizeof colours / sizeof colours[0]; ++c) {
    for (unsigned m = 0; m < 3; ++m) {
      u8 rgba[64], block[16], out[64];
      fill(rgba, colours[c][0], colours[c][1], colours[c][2], 255);
      squish::Compress(rgba, block, methods[m]);
      squish::Decompress(out, block, methods[m]);
      check(same_pixels(rgba, out), "single colour round trip");
    }
  }
}

static void colour_fits() {
  u8 rgba[64];
  palette_block(rgba);
  float metric[3] = {0.2126f, 0.7152f, 0.0722f};
  static int const fits[] = {
      squish::kColourClusterFit, squish::kColourRangeFit,
      squish::kColourIterativeClusterFit,
      squish::kColourClusterFit | squish::kWeightColourByAlpha};
  for (unsigned f = 0; f < 4; ++f) {
    for (int weighted = 0; weighted < 2; ++weighted) {
      u8 block[8], out[64];
      squish::Compress(rgba, block, squish::kDxt1 | fits[f],
                       weighted ? metric : 0);
      squish::Decompress(out, block, squish::kDxt1);
      check(same_pixels(rgba, out), "colour fit reproduces its palette");
    }
  }
}

static void alpha_channels() {
  u8 rgba[64], block[16], out[64];

  // DXT3 keeps 4 bits of alpha: multiples of 17 are exact.
  for (int i = 0; i < 16; ++i) set(rgba + 4 * i, 255, 0, 0, 17 * i);
  squish::Compress(rgba, block, squish::kDxt3);
  squish::Decompress(out, block, squish::kDxt3);
  check(same_pixels(rgba, out), "DXT3 alpha");

  // DXT5's six-value codebook from 40 to 200 plus its 0 and 255 codes.
  static int const alphas[4] = {0, 40, 200, 255};
  for (int i = 0; i < 16; ++i) set(rgba + 4 * i, 0, 255, 0, alphas[i % 4]);
  squish::Compress(rgba, block, squish::kDxt5);
  squish::Decompress(out, block, squish::kDxt5);
  check(same_pixels(rgba, out), "DXT5 alpha");

  // DXT1 makes pixels with alpha below 128 transparent black.
  for (int i = 0; i < 16; ++i)
    set(rgba + 4 * i, 255, 0, 0, i % 2 ? 128 : 127);
  squish::Compress(rgba, block, squish::kDxt1);
  squish::Decompress(out, block, squish::kDxt1);
  bool ok = true;
  for (int i = 0; i < 16; ++i)
    ok = ok && (i % 2 ? is(out + 4 * i, 255, 0, 0, 255)
                      : is(out + 4 * i, 0, 0, 0, 0));
  check(ok, "DXT1 one-bit alpha");
}

static void bc4_bc5() {
  // BC4 stores red, BC5 red then green, each as a DXT5 alpha block,
  // which Decompress with kDxt5 decodes into the alpha channel.  The reds
  // fit the six-value codebook plus 0 and 255, the greens (250 - 34k)
  // the eight-value one.
  static int const reds[4] = {0, 40, 200, 255};
  static int const greens[4] = {12, 80, 182, 250};
  u8 rgba[64];
  for (int i = 0; i < 16; ++i)
    set(rgba + 4 * i, reds[i % 4], greens[i / 4], 7, 9);

  u8 bc4[16] = {0};
  squish::Compress(rgba, bc4, squish::kBc4);
  u8 out[64];
  squish::Decompress(out, bc4, squish::kDxt5);
  bool ok = true;
  for (int i = 0; i < 16; ++i) ok = ok && out[4 * i + 3] == reds[i % 4];
  check(ok, "BC4 red");

  u8 bc5[16];
  squish::Compress(rgba, bc5, squish::kBc5);
  check(std::memcmp(bc5, bc4, 8) == 0, "BC5 red half");
  u8 green[16] = {0};
  std::memcpy(green, bc5 + 8, 8);
  squish::Decompress(out, green, squish::kDxt5);
  ok = true;
  for (int i = 0; i < 16; ++i) ok = ok && out[4 * i + 3] == greens[i / 4];
  check(ok, "BC5 green");
}

static void storage() {
  check(squish::GetStorageRequirements(4, 4, squish::kDxt1) == 8, "4x4 DXT1");
  check(squish::GetStorageRequirements(4, 4, 0) == 8, "flags 0 is DXT1");
  check(squish::GetStorageRequirements(4, 4, squish::kDxt5) == 16, "4x4 DXT5");
  check(squish::GetStorageRequirements(5, 3, squish::kDxt1) == 16, "5x3 DXT1");
  check(squish::GetStorageRequirements(5, 3, squish::kDxt3) == 32, "5x3 DXT3");
  check(squish::GetStorageRequirements(1, 1, squish::kBc4) == 8, "1x1 BC4");
  check(squish::GetStorageRequirements(8, 8, squish::kBc5) == 64, "8x8 BC5");
}

static void images() {
  // A 6x5 image in rows padded to 32 bytes: four partial and whole
  // blocks, each one exact colour.
  enum { W = 6, H = 5, PITCH = 32 };
  static int const colours[4][3] = {
      {255, 0, 0}, {0, 255, 0}, {132, 130, 66}, {255, 255, 255}};
  u8 image[H * PITCH];
  std::memset(image, 0xEE, sizeof image);
  for (int y = 0; y < H; ++y)
    for (int x = 0; x < W; ++x) {
      int const* c = colours[(y / 4) * 2 + x / 4];
      set(image + y * PITCH + 4 * x, c[0], c[1], c[2], 255);
    }

  int const size = squish::GetStorageRequirements(W, H, squish::kDxt1);
  check(size == 32, "6x5 DXT1 storage");
  u8 blocks[32];
  squish::CompressImage(image, W, H, PITCH, blocks, squish::kDxt1);

  u8 out[H * PITCH];
  std::memset(out, 0xEE, sizeof out);
  squish::DecompressImage(out, W, H, PITCH, blocks, squish::kDxt1);
  check(std::memcmp(image, out, sizeof out) == 0,
        "image round trip leaves the row padding alone");

  double colour_mse = -1, alpha_mse = -1;
  squish::ComputeMSE(image, W, H, PITCH, blocks, squish::kDxt1, colour_mse,
                     alpha_mse);
  check(colour_mse == 0 && alpha_mse == 0, "ComputeMSE of an exact image");

  // A diagonal ramp, seven colours on a line per block, is not exact;
  // its error stays small, and ComputeMSE is the mean squared error of
  // what DecompressImage produces (it would weight near-uniform blocks
  // 5x; these blocks are not).
  u8 grad[8 * 8 * 4], back[8 * 8 * 4], dxt[32];
  for (int y = 0; y < 8; ++y)
    for (int x = 0; x < 8; ++x) {
      int t = 4 * (x + y);
      set(grad + 4 * (8 * y + x), 40 + t, 60 + t, 200 - t, 255);
    }
  squish::CompressImage(grad, 8, 8, dxt, squish::kDxt1);
  squish::DecompressImage(back, 8, 8, dxt, squish::kDxt1);
  double sum = 0;
  for (int i = 0; i < 64; ++i)
    for (int k = 0; k < 3; ++k) {
      double d = (double)grad[4 * i + k] - back[4 * i + k];
      sum += d * d;
    }
  squish::ComputeMSE(grad, 8, 8, dxt, squish::kDxt1, colour_mse, alpha_mse);
  check(sum > 0 && colour_mse > 0 && colour_mse < 12 && alpha_mse == 0,
        "gradient MSE");
  check(colour_mse == sum / (64 * 3), "ComputeMSE averages squared error");
}

static void masked() {
  // Masked-off pixels do not influence the fit, and DXT3 zeroes their
  // alpha.
  u8 rgba[64], out[64];
  for (int i = 0; i < 16; ++i)
    set(rgba + 4 * i, i < 8 ? 255 : 37 * i, i < 8 ? 0 : 255 - 13 * i,
        i < 8 ? 0 : 91, 255);
  u8 dxt3[16];
  squish::CompressMasked(rgba, 0x00FF, dxt3, squish::kDxt3);
  squish::Decompress(out, dxt3, squish::kDxt3);
  bool ok = true;
  for (int i = 0; i < 16; ++i)
    ok = ok && (i < 8 ? is(out + 4 * i, 255, 0, 0, 255) : out[4 * i + 3] == 0);
  check(ok, "CompressMasked");
}

int main() {
  decode_blocks();
  single_colours();
  colour_fits();
  alpha_channels();
  bc4_bc5();
  storage();
  images();
  masked();
  if (failures != 0) return 1;
  std::puts("libsquish consumer ok");
  return 0;
}
