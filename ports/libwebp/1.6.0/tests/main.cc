// Builds an animated WebP with libwebpmux's WebPAnimEncoder, reads it
// back through libwebpdemux (container fields, then every frame through
// WebPAnimDecoder), and round-trips an EXIF chunk through WebPMux and
// the demuxer's chunk iterator.
#include <webp/decode.h>
#include <webp/demux.h>
#include <webp/encode.h>
#include <webp/mux.h>

#include <cstdio>
#include <cstring>
#include <vector>

namespace {

const int kW = 20, kH = 14, kFrames = 3;
const int kTimestamps[kFrames + 1] = {0, 100, 250, 400};

bool fail(const char* what) {
  std::fprintf(stderr, "libwebp: %s\n", what);
  return false;
}

// Opaque frames, each different everywhere, so no frame merges into
// its predecessor and the decoder has nothing to blend.
std::vector<uint8_t> frame(int n) {
  std::vector<uint8_t> rgba(kW * kH * 4);
  for (int y = 0; y < kH; ++y)
    for (int x = 0; x < kW; ++x) {
      uint8_t* p = &rgba[(y * kW + x) * 4];
      p[0] = static_cast<uint8_t>(x * 12 + n * 40);
      p[1] = static_cast<uint8_t>(y * 17 + n * 70);
      p[2] = static_cast<uint8_t>((x ^ y) * 9 + n * 5);
      p[3] = 255;
    }
  return rgba;
}

bool animation() {
  WebPAnimEncoderOptions options;
  if (!WebPAnimEncoderOptionsInit(&options)) return fail("encoder options");
  options.anim_params.loop_count = 3;
  WebPAnimEncoder* enc = WebPAnimEncoderNew(kW, kH, &options);
  WebPConfig config;
  if (!enc || !WebPConfigInit(&config)) return fail("WebPAnimEncoderNew");
  config.lossless = 1;
  for (int n = 0; n < kFrames; ++n) {
    std::vector<uint8_t> rgba = frame(n);
    WebPPicture pic;
    if (!WebPPictureInit(&pic)) return fail("picture init");
    pic.width = kW;
    pic.height = kH;
    pic.use_argb = 1;
    if (!WebPPictureImportRGBA(&pic, rgba.data(), kW * 4) ||
        !WebPAnimEncoderAdd(enc, &pic, kTimestamps[n], &config))
      return fail(WebPAnimEncoderGetError(enc));
    WebPPictureFree(&pic);
  }
  WebPData webp;
  WebPDataInit(&webp);
  if (!WebPAnimEncoderAdd(enc, NULL, kTimestamps[kFrames], NULL) ||
      !WebPAnimEncoderAssemble(enc, &webp))
    return fail(WebPAnimEncoderGetError(enc));
  WebPAnimEncoderDelete(enc);

  WebPBitstreamFeatures features;
  if (WebPGetFeatures(webp.bytes, webp.size, &features) != VP8_STATUS_OK ||
      !features.has_animation || features.width != kW)
    return fail("animated features");

  WebPDemuxer* demux = WebPDemux(&webp);
  if (!demux || WebPDemuxGetI(demux, WEBP_FF_CANVAS_WIDTH) != kW ||
      WebPDemuxGetI(demux, WEBP_FF_CANVAS_HEIGHT) != kH ||
      WebPDemuxGetI(demux, WEBP_FF_LOOP_COUNT) != 3 ||
      WebPDemuxGetI(demux, WEBP_FF_FRAME_COUNT) != kFrames ||
      !(WebPDemuxGetI(demux, WEBP_FF_FORMAT_FLAGS) & ANIMATION_FLAG))
    return fail("demux header");
  WebPIterator iter;
  if (!WebPDemuxGetFrame(demux, 1, &iter)) return fail("first frame");
  int n = 0;
  do {
    if (iter.frame_num != n + 1 ||
        iter.duration != kTimestamps[n + 1] - kTimestamps[n])
      return fail("frame durations");
    ++n;
  } while (WebPDemuxNextFrame(&iter));
  WebPDemuxReleaseIterator(&iter);
  WebPDemuxDelete(demux);
  if (n != kFrames) return fail("frame iteration");

  WebPAnimDecoderOptions dec_options;
  if (!WebPAnimDecoderOptionsInit(&dec_options))
    return fail("decoder options");
  dec_options.color_mode = MODE_RGBA;
  WebPAnimDecoder* dec = WebPAnimDecoderNew(&webp, &dec_options);
  WebPAnimInfo info;
  if (!dec || !WebPAnimDecoderGetInfo(dec, &info) ||
      info.frame_count != kFrames || info.loop_count != 3)
    return fail("WebPAnimDecoder info");
  for (n = 0; WebPAnimDecoderHasMoreFrames(dec); ++n) {
    uint8_t* canvas;
    int timestamp;
    if (n == kFrames || !WebPAnimDecoderGetNext(dec, &canvas, &timestamp) ||
        timestamp != kTimestamps[n + 1] ||
        std::memcmp(canvas, frame(n).data(), kW * kH * 4) != 0)
      return fail("decoded frame");
  }
  WebPAnimDecoderDelete(dec);
  WebPDataClear(&webp);
  return n == kFrames || fail("frame count");
}

bool metadata() {
  std::vector<uint8_t> rgba = frame(0);
  uint8_t* still = NULL;
  size_t size = WebPEncodeLosslessRGBA(rgba.data(), kW, kH, kW * 4, &still);
  const char exif_bytes[] = "Exif\0\0MM\0*cabin";
  WebPData image = {still, size}, exif = {
      reinterpret_cast<const uint8_t*>(exif_bytes), sizeof exif_bytes};
  WebPMux* mux = WebPMuxNew();
  WebPData out;
  WebPDataInit(&out);
  if (!mux || WebPMuxSetImage(mux, &image, 1) != WEBP_MUX_OK ||
      WebPMuxSetChunk(mux, "EXIF", &exif, 1) != WEBP_MUX_OK ||
      WebPMuxAssemble(mux, &out) != WEBP_MUX_OK)
    return fail("mux assemble");
  WebPMuxDelete(mux);
  WebPFree(still);

  // Metadata needs the extended format: RIFF, WEBP, then VP8X.
  if (out.size < 16 || std::memcmp(out.bytes + 12, "VP8X", 4) != 0)
    return fail("VP8X container");
  WebPDemuxer* demux = WebPDemux(&out);
  WebPChunkIterator chunk;
  if (!demux || !(WebPDemuxGetI(demux, WEBP_FF_FORMAT_FLAGS) & EXIF_FLAG) ||
      !WebPDemuxGetChunk(demux, "EXIF", 1, &chunk) ||
      chunk.chunk.size != sizeof exif_bytes ||
      std::memcmp(chunk.chunk.bytes, exif_bytes, sizeof exif_bytes) != 0)
    return fail("EXIF chunk");
  WebPDemuxReleaseChunkIterator(&chunk);
  WebPDemuxDelete(demux);

  int width, height;
  uint8_t* pixels = WebPDecodeRGBA(out.bytes, out.size, &width, &height);
  bool same = pixels && width == kW && height == kH &&
              std::memcmp(pixels, rgba.data(), rgba.size()) == 0;
  WebPFree(pixels);
  WebPDataClear(&out);
  return same || fail("image under metadata");
}

}  // namespace

int main() {
  if (WebPGetMuxVersion() != 0x010600 || WebPGetDemuxVersion() != 0x010600)
    return fail("version"), 1;
  return animation() && metadata() ? 0 : 1;
}
