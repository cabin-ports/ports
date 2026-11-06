/* Encodes still images through libwebp's lossless (VP8L), near-lossless
 * and lossy-with-alpha (VP8 + ALPH) paths, decodes them whole,
 * incrementally, cropped and scaled, and converts RGB to YUV through
 * libsharpyuv.  The lossy image is wide enough for the decoder's
 * filtering thread, and its encode runs the alpha and analysis workers;
 * threaded and synchronous runs give the same output. */
#include <webp/decode.h>
#include <webp/encode.h>
#include <webp/sharpyuv/sharpyuv.h>
#include <webp/sharpyuv/sharpyuv_csp.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Odd sizes leave partial macroblocks and chroma samples.  The decoder
 * threads from a width of 512 (src/dec/vp8i_dec.h), and near-lossless
 * applies from a side of 64 (src/enc/near_lossless_enc.c). */
enum { W = 37, H = 23, LW = 523, LH = 19, NW = 64, NH = 16 };

static int fail(const char *what) {
  fprintf(stderr, "libwebp: %s\n", what);
  return 1;
}

static unsigned le32(const uint8_t *p) {
  return p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned)p[3] << 24);
}

/* A RIFF/WEBP container whose first chunk is `fourcc` and whose RIFF
 * size covers exactly the rest of the file. */
static int is_webp(const uint8_t *data, size_t size, const char *fourcc) {
  return size > 20 && !memcmp(data, "RIFF", 4) && le32(data + 4) == size - 8 &&
         !memcmp(data + 8, "WEBP", 4) && !memcmp(data + 12, fourcc, 4);
}

/* Encodes an RGBA picture with `config` through the advanced API. */
static size_t encode(const WebPConfig *config, const uint8_t *rgba, int width,
                     int height, uint8_t **out) {
  WebPPicture pic;
  WebPMemoryWriter writer;
  int ok;
  if (!WebPValidateConfig(config) || !WebPPictureInit(&pic)) return 0;
  pic.width = width;
  pic.height = height;
  pic.use_argb = 1; /* lets the encoder do the YUV conversion itself */
  WebPMemoryWriterInit(&writer);
  pic.writer = WebPMemoryWrite;
  pic.custom_ptr = &writer;
  ok = WebPPictureImportRGBA(&pic, rgba, width * 4) && WebPEncode(config, &pic);
  WebPPictureFree(&pic);
  *out = writer.mem;
  return ok ? writer.size : 0;
}

int main(void) {
  static uint8_t rgba[W * H * 4], wide[LW * LH * 4], noise[NW * NH * 4];
  uint8_t *lossless = NULL, *lossy = NULL, *near = NULL, *pixels;
  size_t lossless_size, lossy_size, near_size, i;
  WebPBitstreamFeatures features;
  WebPDecoderConfig config;
  WebPConfig enc;
  WebPIDecoder *idec;
  VP8StatusCode status = VP8_STATUS_SUSPENDED;
  int x, y, width, height, last_y, stride, changed = 0;
  uint32_t seed = 1;
  double sq_err = 0.0;

  if (WebPGetDecoderVersion() != 0x010600 ||
      WebPGetEncoderVersion() != 0x010600)
    return fail("version");

  for (y = 0; y < H; ++y)
    for (x = 0; x < W; ++x) {
      uint8_t *p = rgba + (y * W + x) * 4;
      p[0] = (uint8_t)(x * 7);
      p[1] = (uint8_t)(y * 11);
      p[2] = (uint8_t)(x * y);
      p[3] = (x + y) % 3 ? 255 : 128;
    }
  /* Smooth colour and alpha for the lossy path. */
  for (y = 0; y < LH; ++y)
    for (x = 0; x < LW; ++x) {
      uint8_t *p = wide + (y * LW + x) * 4;
      p[0] = (uint8_t)(40 + x * 150 / LW);
      p[1] = (uint8_t)(60 + y * 6);
      p[2] = (uint8_t)(200 - x * 100 / LW - y * 2);
      p[3] = (uint8_t)(128 + x * 127 / LW);
    }
  /* Opaque noise: too many colours for a palette, nothing smooth. */
  for (i = 0; i < sizeof noise; ++i) {
    seed = seed * 1103515245u + 12345u;
    noise[i] = i % 4 == 3 ? 255 : (uint8_t)(seed >> 16);
  }

  /* Lossless keeps every pixel, alpha included, in a simple-format
   * (RIFF + VP8L) file. */
  lossless_size = WebPEncodeLosslessRGBA(rgba, W, H, W * 4, &lossless);
  if (!is_webp(lossless, lossless_size, "VP8L")) return fail("VP8L container");
  if (WebPGetFeatures(lossless, lossless_size, &features) != VP8_STATUS_OK ||
      features.width != W || features.height != H || !features.has_alpha ||
      features.has_animation || features.format != 2)
    return fail("VP8L features");
  pixels = WebPDecodeRGBA(lossless, lossless_size, &width, &height);
  if (!pixels || width != W || height != H || memcmp(pixels, rgba, sizeof rgba))
    return fail("lossless round trip");
  WebPFree(pixels);

  /* The incremental decoder, fed a few bytes at a time, matches.  It
   * may finish before a trailing RIFF pad byte. */
  idec = WebPINewRGB(MODE_RGBA, NULL, 0, 0);
  for (i = 0; i < lossless_size && status == VP8_STATUS_SUSPENDED; i += 7)
    status = WebPIAppend(idec, lossless + i,
                         lossless_size - i < 7 ? lossless_size - i : 7);
  if (status != VP8_STATUS_OK || i + 1 < lossless_size)
    return fail("incremental status");
  pixels = WebPIDecGetRGB(idec, &last_y, &width, &height, &stride);
  if (!pixels || last_y != H || stride != W * 4 ||
      memcmp(pixels, rgba, sizeof rgba))
    return fail("incremental decode");
  WebPIDelete(idec);

  /* Cropping a lossless image returns the source pixels. */
  if (!WebPInitDecoderConfig(&config)) return fail("decoder config");
  config.output.colorspace = MODE_RGBA;
  config.options.use_cropping = 1;
  config.options.crop_left = 4;
  config.options.crop_top = 2;
  config.options.crop_width = 10;
  config.options.crop_height = 7;
  if (WebPDecode(lossless, lossless_size, &config) != VP8_STATUS_OK ||
      config.output.width != 10 || config.output.height != 7)
    return fail("crop");
  for (y = 0; y < 7; ++y)
    if (memcmp(config.output.u.RGBA.rgba + y * config.output.u.RGBA.stride,
               rgba + ((y + 2) * W + 4) * 4, 10 * 4))
      return fail("crop pixels");
  WebPFreeDecBuffer(&config.output);
  WebPFree(lossless);

  /* Near-lossless (strongest setting) changes pixels, each channel by
   * at most half its 32-step quantization. */
  if (!WebPConfigInit(&enc)) return fail("encoder config");
  enc.lossless = 1;
  enc.near_lossless = 0;
  near_size = encode(&enc, noise, NW, NH, &near);
  pixels = near_size ? WebPDecodeRGBA(near, near_size, &width, &height) : NULL;
  if (!pixels || width != NW || height != NH) return fail("near-lossless");
  for (i = 0; i < sizeof noise; ++i) {
    if (abs(pixels[i] - noise[i]) > 16) return fail("near-lossless error");
    changed += pixels[i] != noise[i];
  }
  if (!changed) return fail("near-lossless had no effect");
  WebPFree(pixels);
  WebPFree(near);

  /* Lossy with alpha: an extended-format (VP8X, then ALPH) file whose
   * colour decodes close to the source and whose alpha, at the default
   * alpha_quality of 100, decodes exactly. */
  if (!WebPConfigInit(&enc)) return fail("encoder config");
  enc.quality = 90;
  enc.thread_level = 1;
  enc.use_sharp_yuv = 1;
  lossy_size = encode(&enc, wide, LW, LH, &lossy);
  if (!is_webp(lossy, lossy_size, "VP8X") || memcmp(lossy + 30, "ALPH", 4))
    return fail("VP8X + ALPH container");
  if (WebPGetFeatures(lossy, lossy_size, &features) != VP8_STATUS_OK ||
      features.width != LW || features.height != LH || !features.has_alpha ||
      features.format != 1)
    return fail("lossy features");
  if (!WebPInitDecoderConfig(&config)) return fail("decoder config");
  config.output.colorspace = MODE_RGBA;
  config.options.use_threads = 1;
  if (WebPDecode(lossy, lossy_size, &config) != VP8_STATUS_OK)
    return fail("lossy decode");
  for (y = 0; y < LH; ++y)
    for (x = 0; x < LW * 4; ++x) {
      const uint8_t got =
          config.output.u.RGBA.rgba[y * config.output.u.RGBA.stride + x];
      const uint8_t want = wide[y * LW * 4 + x];
      if (x % 4 == 3) {
        if (got != want) return fail("lossy alpha");
      } else {
        sq_err += (double)(got - want) * (got - want);
      }
    }
  WebPFreeDecBuffer(&config.output);
  if (10.0 * log10(255.0 * 255.0 / (sq_err / (LW * LH * 3))) < 35.0)
    return fail("lossy PSNR");
  if (!WebPInitDecoderConfig(&config)) return fail("decoder config");
  config.options.use_scaling = 1;
  config.options.scaled_width = 131;
  config.options.scaled_height = 5;
  if (WebPDecode(lossy, lossy_size, &config) != VP8_STATUS_OK ||
      config.output.width != 131 || config.output.height != 5)
    return fail("scaled decode");
  WebPFreeDecBuffer(&config.output);

  /* Truncated input is an error, never a picture. */
  if (WebPDecodeRGBA(lossy, lossy_size / 2, &width, &height))
    return fail("truncated input decoded");
  WebPFree(lossy);

  /* libsharpyuv: on a flat grey the WebP matrix gives
   * Y = (56318 * 128 + (16 << 16) + (1 << 15)) >> 16 = 126 and neutral
   * chroma. */
  if (SharpYuvGetVersion() != SHARPYUV_VERSION ||
      SHARPYUV_VERSION != SHARPYUV_MAKE_VERSION(0, 4, 2))
    return fail("sharpyuv version");
  {
    uint8_t grey[8 * 6 * 3], yp[8 * 6], up[4 * 3], vp[4 * 3];
    memset(grey, 128, sizeof grey);
    if (!SharpYuvConvert(grey, grey + 1, grey + 2, 3, 8 * 3, 8, yp, 8, up, 4,
                         vp, 4, 8, 8, 6,
                         SharpYuvGetConversionMatrix(kSharpYuvMatrixWebp)))
      return fail("SharpYuvConvert");
    for (i = 0; i < sizeof yp; ++i)
      if (yp[i] != 126) return fail("sharpyuv luma");
    for (i = 0; i < sizeof up; ++i)
      if (up[i] != 128 || vp[i] != 128) return fail("sharpyuv chroma");
  }
  return 0;
}
