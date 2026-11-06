// Round-trips six synthetic 48x32 frames through the legacy theora.h API
// that the theora library keeps for pre-1.0 applications.
#include <theora/theora.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

const int kW = 48;
const int kH = 32;
const int kFrames = 6;
int failures = 0;

void check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

// Vertical bands that slide down a row per frame, so a picture decoded
// upside down or late would not match.
unsigned char luma(int x, int y, int f) {
  return static_cast<unsigned char>(40 + ((y + f) * 5) % 160 + x / 4);
}
unsigned char chroma(int x, int y) {
  return static_cast<unsigned char>(128 + x - y);
}

struct Packet {
  std::vector<unsigned char> data;
  ogg_packet op;
};

Packet copy(const ogg_packet& op) {
  Packet p;
  p.data.assign(op.packet, op.packet + op.bytes);
  p.op = op;
  return p;
}

double psnr(const yuv_buffer& yuv, int f) {
  double se = 0;
  int n = 0;
  for (int y = 0; y < yuv.y_height; ++y)
    for (int x = 0; x < yuv.y_width; ++x, ++n) {
      int d = yuv.y[y * yuv.y_stride + x] - luma(x, y, f);
      se += d * d;
    }
  for (int y = 0; y < yuv.uv_height; ++y)
    for (int x = 0; x < yuv.uv_width; ++x, n += 2) {
      int du = yuv.u[y * yuv.uv_stride + x] - chroma(x, y);
      int dv = yuv.v[y * yuv.uv_stride + x] - chroma(x, y);
      se += du * du + dv * dv;
    }
  return se == 0 ? 99 : 10 * std::log10(255.0 * 255.0 * n / se);
}

}  // namespace

int main() {
  check(std::string(theora_version_string()) ==
            "Xiph.Org libtheora 1.2.0 20250329 (Ptalarbvorm)",
        "version string");
  check(theora_version_number() == 0x030201, "bitstream version number");

  theora_info ti;
  theora_info_init(&ti);
  ti.width = ti.frame_width = kW;
  ti.height = ti.frame_height = kH;
  ti.offset_x = ti.offset_y = 0;
  ti.fps_numerator = 30000;
  ti.fps_denominator = 1001;
  ti.aspect_numerator = ti.aspect_denominator = 1;
  ti.colorspace = OC_CS_UNSPECIFIED;
  ti.pixelformat = OC_PF_420;
  ti.quality = 40;
  ti.keyframe_auto_p = 0;
  ti.keyframe_frequency = ti.keyframe_frequency_force = 3;

  theora_state enc;
  check(theora_encode_init(&enc, &ti) == 0, "theora_encode_init");
  std::vector<Packet> packets;
  ogg_packet op;
  check(theora_encode_header(&enc, &op) == 0, "theora_encode_header");
  packets.push_back(copy(op));
  theora_comment tc;
  theora_comment_init(&tc);
  char tag[] = "ENCODER", value[] = "cabin";
  theora_comment_add_tag(&tc, tag, value);
  check(theora_encode_comment(&tc, &op) == 0, "theora_encode_comment");
  packets.push_back(copy(op));
  std::free(op.packet);  // The caller owns the comment packet.
  theora_comment_clear(&tc);
  check(theora_encode_tables(&enc, &op) == 0, "theora_encode_tables");
  packets.push_back(copy(op));

  std::vector<unsigned char> y(kW * kH), u(kW * kH / 4), v(kW * kH / 4);
  yuv_buffer in;
  in.y_width = kW;
  in.y_height = kH;
  in.y_stride = kW;
  in.uv_width = kW / 2;
  in.uv_height = kH / 2;
  in.uv_stride = kW / 2;
  in.y = y.data();
  in.u = u.data();
  in.v = v.data();
  for (int f = 0; f < kFrames; ++f) {
    for (int r = 0; r < kH; ++r)
      for (int c = 0; c < kW; ++c) y[r * kW + c] = luma(c, r, f);
    for (int r = 0; r < kH / 2; ++r)
      for (int c = 0; c < kW / 2; ++c)
        u[r * kW / 2 + c] = v[r * kW / 2 + c] = chroma(c, r);
    check(theora_encode_YUVin(&enc, &in) == 0, "theora_encode_YUVin");
    check(theora_encode_packetout(&enc, f == kFrames - 1, &op) == 1,
          "theora_encode_packetout");
    packets.push_back(copy(op));
  }
  theora_clear(&enc);
  theora_info_clear(&ti);
  for (Packet& p : packets) p.op.packet = p.data.data();

  theora_info di;
  theora_info_init(&di);
  theora_comment dc;
  theora_comment_init(&dc);
  for (int i = 0; i < 3; ++i) {
    check(theora_packet_isheader(&packets[i].op) == 1, "header flag");
    check(theora_decode_header(&di, &dc, &packets[i].op) == 0,
          "theora_decode_header");
  }
  check(di.width == kW && di.height == kH, "encoded size");
  check(di.frame_width == kW && di.frame_height == kH, "frame size");
  check(di.fps_numerator == 30000 && di.fps_denominator == 1001, "frame rate");
  check(di.version_major == 3 && di.version_minor == 2 &&
            di.version_subminor == 1,
        "bitstream version");
  check(dc.comments == 1, "comment count");
  const char* encoder = theora_comment_query(&dc, tag, 0);
  check(encoder != nullptr && std::strcmp(encoder, "cabin") == 0, "comment");

  theora_state dec;
  check(theora_decode_init(&dec, &di) == 0, "theora_decode_init");
  for (int f = 0; f < kFrames; ++f) {
    ogg_packet& p = packets[3 + f].op;
    check(theora_packet_isheader(&p) == 0, "data flag");
    check(theora_packet_iskeyframe(&p) == (f % 3 == 0), "keyframes");
    check(theora_decode_packetin(&dec, &p) == 0, "theora_decode_packetin");
    check(theora_granule_frame(&dec, dec.granulepos) == f, "granule frame");
    yuv_buffer out;
    check(theora_decode_YUVout(&dec, &out) == 0, "theora_decode_YUVout");
    check(out.y_width == kW && out.y_height == kH, "decoded size");
    check(psnr(out, f) > 30, "PSNR");
  }
  check(std::fabs(theora_granule_time(&dec, dec.granulepos) -
                  6 * 1001 / 30000.0) < 1e-9,
        "granule time");
  theora_clear(&dec);
  theora_comment_clear(&dc);
  theora_info_clear(&di);

  // A header packet without the "theora" signature, and an encoder size
  // that is not a multiple of 16.
  unsigned char bogus[] = {0x80, 'v', 'o', 'r', 'b', 'i', 's', 0, 0};
  ogg_packet bad = {};
  bad.packet = bogus;
  bad.bytes = sizeof bogus;
  bad.b_o_s = 1;
  theora_info bi;
  theora_info_init(&bi);
  theora_comment bc;
  theora_comment_init(&bc);
  check(theora_decode_header(&bi, &bc, &bad) == OC_NOTFORMAT,
        "non-Theora header rejected");
  theora_comment_clear(&bc);
  theora_info_clear(&bi);
  theora_info odd;
  theora_info_init(&odd);
  odd.width = odd.frame_width = 40;
  odd.height = odd.frame_height = 32;
  odd.fps_numerator = odd.fps_denominator = 1;
  theora_state none;
  check(theora_encode_init(&none, &odd) == OC_EINVAL, "bad size rejected");

  if (failures != 0) return 1;
  std::puts("libtheora C++ consumer ok");
  return 0;
}
