// C consumer: encodes a short synthetic I420 clip with the encoder's base
// parameters through the C vtables, decodes the bitstream, and checks the
// round trip against the source and the exact bytes of both directions.
#include <wels/codec_api.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum { W = 64, H = 48, FRAMES = 6, YSIZE = W * H, CSIZE = W * H / 4 };

static int failures = 0;

#define CHECK(cond)                                                   \
  do {                                                                \
    if (!(cond)) {                                                    \
      printf("main.c:%d: check failed: %s\n", __LINE__, #cond);       \
      ++failures;                                                     \
    }                                                                 \
  } while (0)

static unsigned char frames[FRAMES][YSIZE + 2 * CSIZE];
static unsigned char stream[FRAMES][16 * 1024];
static int sizes[FRAMES];

// Upstream keeps one encoder output hash per single-layer configuration
// for every platform (test/api/encoder_test.cpp:140-160).  The hashes
// pin what PSNR cannot see: a kernel the encoder and the decoder share,
// or one that only steers the encoder's choices.
#define STREAM_FNV 0x63ea66b9u
#define OUTPUT_FNV 0xa4439acfu

static uint32_t fnv1a(uint32_t h, const unsigned char *p, int n) {
  for (int i = 0; i < n; ++i) h = (h ^ p[i]) * 16777619u;
  return h;
}

// A luma gradient with a bright 16x16 square moving half a pixel right
// and one pixel down per frame, over smooth chroma ramps.
static void make_frame(int t, unsigned char *yuv) {
  unsigned char *u = yuv + YSIZE, *v = u + CSIZE;
  const int left = 16 + t;  // in half pixels
  for (int r = 0; r < H; ++r)
    for (int c = 0; c < W; ++c) {
      // The square's cover of this pixel, in half pixels.
      int lo = 2 * c > left ? 2 * c : left;
      int hi = 2 * c + 2 < left + 32 ? 2 * c + 2 : left + 32;
      int cover = r >= 8 + t && r < 24 + t && hi > lo ? hi - lo : 0;
      yuv[r * W + c] = (unsigned char)(((40 + c + r) * (2 - cover) + 230 * cover) / 2);
    }
  for (int r = 0; r < H / 2; ++r)
    for (int c = 0; c < W / 2; ++c) {
      u[r * W / 2 + c] = (unsigned char)(100 + c);
      v[r * W / 2 + c] = (unsigned char)(150 - r);
    }
}

static double psnr(const unsigned char *src, int w, int h, const unsigned char *dst,
                   int stride) {
  double sse = 0;
  for (int r = 0; r < h; ++r)
    for (int c = 0; c < w; ++c) {
      double d = (double)src[r * w + c] - dst[r * stride + c];
      sse += d * d;
    }
  return sse == 0 ? 99.0 : 10 * log10(255.0 * 255.0 * w * h / sse);
}

static void encode(void) {
  ISVCEncoder *enc = NULL;
  CHECK(WelsCreateSVCEncoder(&enc) == 0 && enc != NULL);
  if (!enc) return;
  SEncParamBase param;
  memset(&param, 0, sizeof param);
  param.iUsageType = CAMERA_VIDEO_REAL_TIME;
  param.iPicWidth = W;
  param.iPicHeight = H;
  param.iTargetBitrate = 2000000;
  param.iRCMode = RC_QUALITY_MODE;
  param.fMaxFrameRate = 30;
  CHECK((*enc)->Initialize(enc, &param) == cmResultSuccess);

  int types[FRAMES];
  uint32_t hash = 2166136261u;
  for (int t = 0; t < FRAMES; ++t) {
    make_frame(t, frames[t]);
    SSourcePicture pic;
    memset(&pic, 0, sizeof pic);
    pic.iColorFormat = videoFormatI420;
    pic.iStride[0] = W;
    pic.iStride[1] = pic.iStride[2] = W / 2;
    pic.pData[0] = frames[t];
    pic.pData[1] = frames[t] + YSIZE;
    pic.pData[2] = frames[t] + YSIZE + CSIZE;
    pic.iPicWidth = W;
    pic.iPicHeight = H;
    pic.uiTimeStamp = t * 33;
    SFrameBSInfo info;
    memset(&info, 0, sizeof info);
    CHECK((*enc)->EncodeFrame(enc, &pic, &info) == cmResultSuccess);
    types[t] = info.eFrameType;

    // Each layer's NAL units are contiguous in its buffer.
    int size = 0;
    for (int l = 0; l < info.iLayerNum; ++l) {
      const SLayerBSInfo *layer = &info.sLayerInfo[l];
      int len = 0;
      for (int n = 0; n < layer->iNalCount; ++n) len += layer->pNalLengthInByte[n];
      if (size + len > (int)sizeof stream[t]) {
        CHECK(!"frame fits the buffer");
        break;
      }
      memcpy(stream[t] + size, layer->pBsBuf, len);
      size += len;
    }
    CHECK(size == info.iFrameSizeInBytes);
    sizes[t] = size;
    hash = fnv1a(hash, stream[t], size);
  }
  (*enc)->Uninitialize(enc);
  WelsDestroySVCEncoder(enc);

  // An IDR opening with a start code and the sequence parameter set, then
  // P frames that predict the moving square instead of recoding it.
  CHECK(types[0] == videoFrameTypeIDR);
  CHECK(memcmp(stream[0], "\0\0\0\1", 4) == 0 && (stream[0][4] & 0x1f) == 7);
  for (int t = 1; t < FRAMES; ++t) {
    CHECK(types[t] == videoFrameTypeP);
    CHECK(sizes[t] > 0 && sizes[t] < sizes[0]);
  }
  if (hash != STREAM_FNV) printf("bitstream FNV-1a: 0x%08lx\n", (unsigned long)hash);
  CHECK(hash == STREAM_FNV);
}

static void decode(void) {
  ISVCDecoder *dec = NULL;
  CHECK(WelsCreateDecoder(&dec) == 0 && dec != NULL);
  if (!dec) return;
  SDecodingParam param;
  memset(&param, 0, sizeof param);
  param.sVideoProperty.eVideoBsType = VIDEO_BITSTREAM_AVC;
  CHECK((*dec)->Initialize(dec, &param) == 0);

  int decoded = 0;
  uint32_t hash = 2166136261u;
  for (int t = 0; t < FRAMES; ++t) {
    unsigned char *dst[3] = {NULL, NULL, NULL};
    SBufferInfo info;
    memset(&info, 0, sizeof info);
    CHECK((*dec)->DecodeFrameNoDelay(dec, stream[t], sizes[t], dst, &info) == dsErrorFree);
    if (info.iBufferStatus != 1) continue;
    ++decoded;
    const SSysMEMBuffer *buf = &info.UsrData.sSystemBuffer;
    CHECK(buf->iWidth == W && buf->iHeight == H);
    double y = psnr(frames[t], W, H, dst[0], buf->iStride[0]);
    double u = psnr(frames[t] + YSIZE, W / 2, H / 2, dst[1], buf->iStride[1]);
    double v = psnr(frames[t] + YSIZE + CSIZE, W / 2, H / 2, dst[2], buf->iStride[1]);
    if (!(y > 35 && u > 35 && v > 35))
      printf("frame %d: PSNR Y %.1f U %.1f V %.1f\n", t, y, u, v);
    CHECK(y > 35 && u > 35 && v > 35);
    for (int r = 0; r < H; ++r) hash = fnv1a(hash, dst[0] + r * buf->iStride[0], W);
    for (int p = 1; p < 3; ++p)
      for (int r = 0; r < H / 2; ++r) hash = fnv1a(hash, dst[p] + r * buf->iStride[1], W / 2);
  }
  CHECK(decoded == FRAMES);
  if (hash != OUTPUT_FNV) printf("decoded FNV-1a: 0x%08lx\n", (unsigned long)hash);
  CHECK(hash == OUTPUT_FNV);
  (*dec)->Uninitialize(dec);
  WelsDestroyDecoder(dec);
}

int main(void) {
  OpenH264Version version = WelsGetCodecVersion();
  CHECK(version.uMajor == 2 && version.uMinor == 7 && version.uRevision == 0);
  encode();
  decode();
  printf("openh264 (C): %s\n", failures == 0 ? "ok" : "FAILED");
  return failures == 0 ? 0 : 1;
}
