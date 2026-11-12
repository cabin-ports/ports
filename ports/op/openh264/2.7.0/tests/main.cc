// C++ consumer: encodes a synthetic I420 clip with the extended parameters
// (CABAC, two slices on two threads, an IDR every four frames), decodes the
// High-profile bitstream, flushing the pictures the decoder holds back for
// reordering, and checks the round trip.
#include <wels/codec_api.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

// Upstream codes a picture of 48 macroblocks or fewer as one slice.
constexpr int W = 128, H = 112, FRAMES = 8, YSIZE = W * H, CSIZE = W * H / 4;

int failures = 0;

#define CHECK(cond)                                                   \
  do {                                                                \
    if (!(cond)) {                                                    \
      std::printf("main.cc:%d: check failed: %s\n", __LINE__, #cond); \
      ++failures;                                                     \
    }                                                                 \
  } while (0)

// A luma gradient with a bright 16x16 square moving down and right, over
// smooth chroma ramps.
std::vector<unsigned char> make_frame(int t) {
  std::vector<unsigned char> yuv(YSIZE + 2 * CSIZE);
  unsigned char* u = yuv.data() + YSIZE;
  unsigned char* v = u + CSIZE;
  for (int r = 0; r < H; ++r)
    for (int c = 0; c < W; ++c) {
      bool square = c >= 8 + 2 * t && c < 24 + 2 * t && r >= 8 + t && r < 24 + t;
      yuv[r * W + c] = static_cast<unsigned char>(square ? 230 : 40 + (c + r) / 2);
    }
  for (int r = 0; r < H / 2; ++r)
    for (int c = 0; c < W / 2; ++c) {
      u[r * W / 2 + c] = static_cast<unsigned char>(100 + c / 2);
      v[r * W / 2 + c] = static_cast<unsigned char>(150 - r / 2);
    }
  return yuv;
}

double psnr(const unsigned char* src, int w, int h, const unsigned char* dst, int stride) {
  double sse = 0;
  for (int r = 0; r < h; ++r)
    for (int c = 0; c < w; ++c) {
      double d = double(src[r * w + c]) - dst[r * stride + c];
      sse += d * d;
    }
  return sse == 0 ? 99.0 : 10 * std::log10(255.0 * 255.0 * w * h / sse);
}

struct Frame {
  std::vector<unsigned char> bits;
  int type = 0;
  int slices = 0;
};

std::vector<Frame> encode(const std::vector<std::vector<unsigned char>>& clip) {
  std::vector<Frame> out;
  ISVCEncoder* enc = nullptr;
  CHECK(WelsCreateSVCEncoder(&enc) == 0 && enc != nullptr);
  if (!enc) return out;
  SEncParamExt param;
  CHECK(enc->GetDefaultParams(&param) == cmResultSuccess);
  param.iUsageType = CAMERA_VIDEO_REAL_TIME;
  param.iPicWidth = W;
  param.iPicHeight = H;
  param.iTargetBitrate = 2000000;
  param.iRCMode = RC_QUALITY_MODE;
  param.fMaxFrameRate = 30;
  param.iEntropyCodingModeFlag = 1;
  param.uiIntraPeriod = 4;
  param.iMultipleThreadIdc = 2;
  param.bEnableFrameSkip = false;
  param.iSpatialLayerNum = 1;
  SSpatialLayerConfig& layer = param.sSpatialLayers[0];
  layer.iVideoWidth = W;
  layer.iVideoHeight = H;
  layer.fFrameRate = 30;
  layer.iSpatialBitrate = 2000000;
  layer.sSliceArgument.uiSliceMode = SM_FIXEDSLCNUM_SLICE;
  layer.sSliceArgument.uiSliceNum = 2;
  CHECK(enc->InitializeExt(&param) == cmResultSuccess);

  for (int t = 0; t < FRAMES; ++t) {
    SSourcePicture pic = {};
    pic.iColorFormat = videoFormatI420;
    pic.iStride[0] = W;
    pic.iStride[1] = pic.iStride[2] = W / 2;
    pic.pData[0] = const_cast<unsigned char*>(clip[t].data());
    pic.pData[1] = pic.pData[0] + YSIZE;
    pic.pData[2] = pic.pData[1] + CSIZE;
    pic.iPicWidth = W;
    pic.iPicHeight = H;
    pic.uiTimeStamp = t * 33;
    SFrameBSInfo info = {};
    CHECK(enc->EncodeFrame(&pic, &info) == cmResultSuccess);
    Frame frame;
    frame.type = info.eFrameType;
    for (int l = 0; l < info.iLayerNum; ++l) {
      const SLayerBSInfo& bs = info.sLayerInfo[l];
      int len = 0;
      for (int n = 0; n < bs.iNalCount; ++n) len += bs.pNalLengthInByte[n];
      frame.bits.insert(frame.bits.end(), bs.pBsBuf, bs.pBsBuf + len);
      if (bs.uiLayerType == VIDEO_CODING_LAYER) frame.slices += bs.iNalCount;
    }
    CHECK(int(frame.bits.size()) == info.iFrameSizeInBytes);
    out.push_back(frame);
  }
  enc->Uninitialize();
  WelsDestroySVCEncoder(enc);
  return out;
}

void decode(const std::vector<std::vector<unsigned char>>& clip,
            const std::vector<Frame>& stream) {
  ISVCDecoder* dec = nullptr;
  CHECK(WelsCreateDecoder(&dec) == 0 && dec != nullptr);
  if (!dec) return;
  SDecodingParam param = {};
  param.sVideoProperty.eVideoBsType = VIDEO_BITSTREAM_AVC;
  CHECK(dec->Initialize(&param) == 0);

  // Pictures come out in display order, which here is the coding order.
  size_t decoded = 0;
  auto check_output = [&](const SBufferInfo& info) {
    if (info.iBufferStatus != 1) return;
    CHECK(decoded < clip.size());
    if (decoded >= clip.size()) return;
    const SSysMEMBuffer& buf = info.UsrData.sSystemBuffer;
    CHECK(buf.iWidth == W && buf.iHeight == H);
    const unsigned char* src = clip[decoded].data();
    double y = psnr(src, W, H, info.pDst[0], buf.iStride[0]);
    double u = psnr(src + YSIZE, W / 2, H / 2, info.pDst[1], buf.iStride[1]);
    double v = psnr(src + YSIZE + CSIZE, W / 2, H / 2, info.pDst[2], buf.iStride[1]);
    if (!(y > 35 && u > 35 && v > 35))
      std::printf("frame %zu: PSNR Y %.1f U %.1f V %.1f\n", decoded, y, u, v);
    CHECK(y > 35 && u > 35 && v > 35);
    ++decoded;
  };
  for (const Frame& frame : stream) {
    unsigned char* dst[3] = {};
    SBufferInfo info = {};
    CHECK(dec->DecodeFrameNoDelay(frame.bits.data(), int(frame.bits.size()), dst, &info) ==
          dsErrorFree);
    check_output(info);
  }
  int eos = 1;
  CHECK(dec->SetOption(DECODER_OPTION_END_OF_STREAM, &eos) == cmResultSuccess);
  int remaining = 0;
  CHECK(dec->GetOption(DECODER_OPTION_NUM_OF_FRAMES_REMAINING_IN_BUFFER, &remaining) ==
        cmResultSuccess);
  for (int i = 0; i < remaining; ++i) {
    unsigned char* dst[3] = {};
    SBufferInfo info = {};
    CHECK(dec->FlushFrame(dst, &info) == dsErrorFree);
    check_output(info);
  }
  CHECK(decoded == clip.size());
  dec->Uninitialize();
  WelsDestroyDecoder(dec);
}

}  // namespace

int main() {
  OpenH264Version version;
  WelsGetCodecVersionEx(&version);
  CHECK(version.uMajor == 2 && version.uMinor == 7 && version.uRevision == 0);

  std::vector<std::vector<unsigned char>> clip;
  for (int t = 0; t < FRAMES; ++t) clip.push_back(make_frame(t));
  std::vector<Frame> stream = encode(clip);
  CHECK(int(stream.size()) == FRAMES);
  if (int(stream.size()) == FRAMES) {
    // uiIntraPeriod = 4: IDRs at frames 0 and 4, P frames between, each
    // picture coded as two slices.
    for (int t = 0; t < FRAMES; ++t) {
      CHECK(stream[t].type == (t % 4 == 0 ? videoFrameTypeIDR : videoFrameTypeP));
      CHECK(stream[t].slices == 2);
    }
    CHECK(stream[1].bits.size() < stream[0].bits.size());
    decode(clip, stream);
  }
  std::printf("openh264 (C++): %s\n", failures == 0 ? "ok" : "FAILED");
  return failures == 0 ? 0 : 1;
}
