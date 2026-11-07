// Encodes half a second of a mono tone with vorbisenc's managed-bitrate
// (ABR) mode, then decodes it with libvorbis's low-level synthesis API,
// pulling pages through ogg_sync as a streaming player would.
#include <vorbis/codec.h>
#include <vorbis/vorbisenc.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

const long kRate = 22050;
const long kFrames = 11025;
const double kTwoPi = 6.283185307179586;
int failures = 0;

void check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

float source(long i) {
  return static_cast<float>(0.25 * std::sin(kTwoPi * 1000.0 * i / kRate));
}

void append_page(std::vector<unsigned char>& out, const ogg_page& og) {
  out.insert(out.end(), og.header, og.header + og.header_len);
  out.insert(out.end(), og.body, og.body + og.body_len);
}

std::vector<unsigned char> encode() {
  std::vector<unsigned char> out;
  vorbis_info vi;
  vorbis_info_init(&vi);
  check(vorbis_encode_setup_managed(&vi, 1, kRate, -1, 48000, -1) == 0,
        "encode_setup_managed");
  // vorbis_encode_ctl() refuses this request once setup_init has run: its
  // low nibble is nonzero, which vorbisenc.c takes for a write.
  ovectl_ratemanage2_arg manage;
  check(vorbis_encode_ctl(&vi, OV_ECTL_RATEMANAGE2_GET, &manage) == 0,
        "OV_ECTL_RATEMANAGE2_GET");
  check(manage.management_active == 1, "bitrate management active");
  check(manage.bitrate_average_kbps == 48, "average bitrate");
  check(vorbis_encode_setup_init(&vi) == 0, "encode_setup_init");
  check(vi.bitrate_nominal == 48000, "nominal bitrate");

  vorbis_comment vc;
  vorbis_comment_init(&vc);
  vorbis_comment_add_tag(&vc, "ARTIST", "cabin");
  vorbis_comment_add(&vc, "COMMENT=tone");
  vorbis_dsp_state vd;
  vorbis_analysis_init(&vd, &vi);
  vorbis_block vb;
  vorbis_block_init(&vd, &vb);
  ogg_stream_state os;
  ogg_stream_init(&os, 42);

  ogg_packet header, comment, codebooks, op;
  vorbis_analysis_headerout(&vd, &vc, &header, &comment, &codebooks);
  ogg_stream_packetin(&os, &header);
  ogg_stream_packetin(&os, &comment);
  ogg_stream_packetin(&os, &codebooks);
  ogg_page og;
  while (ogg_stream_flush(&os, &og) != 0) append_page(out, og);

  for (long pos = 0;; pos += 512) {
    long n = pos < kFrames ? std::min(512L, kFrames - pos) : 0;
    if (n > 0) {
      float** in = vorbis_analysis_buffer(&vd, static_cast<int>(n));
      for (long i = 0; i < n; ++i) in[0][i] = source(pos + i);
    }
    vorbis_analysis_wrote(&vd, static_cast<int>(n));
    while (vorbis_analysis_blockout(&vd, &vb) == 1) {
      vorbis_analysis(&vb, nullptr);
      vorbis_bitrate_addblock(&vb);
      while (vorbis_bitrate_flushpacket(&vd, &op))
        ogg_stream_packetin(&os, &op);
    }
    if (n == 0) break;
  }
  while (ogg_stream_flush(&os, &og) != 0) append_page(out, og);

  ogg_stream_clear(&os);
  vorbis_block_clear(&vb);
  vorbis_dsp_clear(&vd);
  vorbis_comment_clear(&vc);
  vorbis_info_clear(&vi);
  return out;
}

}  // namespace

int main() {
  const std::vector<unsigned char> stream = encode();

  ogg_sync_state oy;
  ogg_sync_init(&oy);
  ogg_stream_state os = {};
  bool stream_ready = false;
  vorbis_info vi;
  vorbis_info_init(&vi);
  vorbis_comment vc;
  vorbis_comment_init(&vc);
  vorbis_dsp_state vd = {};
  vorbis_block vb = {};
  int headers = 0;
  bool eos_seen = false;
  std::vector<float> pcm_out;

  for (std::size_t fed = 0; fed < stream.size();) {
    std::size_t n = std::min<std::size_t>(700, stream.size() - fed);
    char* buf = ogg_sync_buffer(&oy, static_cast<long>(n));
    std::memcpy(buf, stream.data() + fed, n);
    ogg_sync_wrote(&oy, static_cast<long>(n));
    fed += n;

    ogg_page og;
    while (ogg_sync_pageout(&oy, &og) == 1) {
      if (!stream_ready) {
        ogg_stream_init(&os, ogg_page_serialno(&og));
        stream_ready = true;
      }
      check(ogg_stream_pagein(&os, &og) == 0, "pagein");
      ogg_packet op;
      while (ogg_stream_packetout(&os, &op) == 1) {
        if (headers < 3) {
          if (headers == 0)
            check(vorbis_synthesis_idheader(&op) == 1, "idheader");
          check(vorbis_synthesis_headerin(&vi, &vc, &op) == 0, "headerin");
          if (++headers == 3) {
            check(vorbis_synthesis_init(&vd, &vi) == 0, "synthesis_init");
            vorbis_block_init(&vd, &vb);
          }
          continue;
        }
        check(vorbis_synthesis(&vb, &op) == 0, "synthesis");
        check(vorbis_synthesis_blockin(&vd, &vb) == 0, "blockin");
        float** pcm;
        int got;
        while ((got = vorbis_synthesis_pcmout(&vd, &pcm)) > 0) {
          pcm_out.insert(pcm_out.end(), pcm[0], pcm[0] + got);
          vorbis_synthesis_read(&vd, got);
        }
        if (op.e_o_s) eos_seen = true;
      }
    }
  }

  check(headers == 3, "three headers");
  check(eos_seen, "end of stream");
  check(os.serialno == 42, "serial number");
  check(vi.channels == 1 && vi.rate == kRate, "decoded info");
  check(vi.bitrate_nominal == 48000, "decoded nominal bitrate");
  // 48 kbit/s mono at 22050 Hz falls between the 28000 and 50000 entries
  // of rate_mapping_22_uncoupled, so setting 1 of blocksize_16_short and
  // blocksize_16_long applies (modes/setup_22.h, modes/setup_16.h).
  check(vorbis_info_blocksize(&vi, 0) == 512, "short blocksize");
  check(vorbis_info_blocksize(&vi, 1) == 1024, "long blocksize");
  check(vc.comments == 2, "comment count");
  const char* artist = vorbis_comment_query(&vc, "artist", 0);
  check(artist != nullptr && std::strcmp(artist, "cabin") == 0, "artist");
  const char* note = vorbis_comment_query(&vc, "COMMENT", 0);
  check(note != nullptr && std::strcmp(note, "tone") == 0, "comment");

  // libvorbis trims the stream to the last page's granule position.
  check(static_cast<long>(pcm_out.size()) == kFrames, "decoded length");
  check(vd.granulepos == kFrames, "final granule position");
  check(std::fabs(vorbis_granule_time(&vd, kFrames) - 0.5) < 1e-12,
        "granule time");
  if (static_cast<long>(pcm_out.size()) == kFrames) {
    double signal = 0, noise = 0;
    for (long i = 0; i < kFrames; ++i) {
      double want = source(i), d = pcm_out[i] - want;
      signal += want * want;
      noise += d * d;
    }
    check(10 * std::log10(signal / noise) > 20, "SNR");
  }

  // A packet that claims to be a header but lacks the "vorbis" signature.
  unsigned char bogus[] = {1, 'n', 'o', 't', 'v', 'o', 'r', 'b', 'i', 's'};
  ogg_packet bad = {};
  bad.packet = bogus;
  bad.bytes = sizeof bogus;
  bad.b_o_s = 1;
  vorbis_info vi2;
  vorbis_info_init(&vi2);
  vorbis_comment vc2;
  vorbis_comment_init(&vc2);
  check(vorbis_synthesis_idheader(&bad) == 0, "bogus idheader");
  check(vorbis_synthesis_headerin(&vi2, &vc2, &bad) == OV_ENOTVORBIS,
        "bogus header rejected");
  vorbis_comment_clear(&vc2);
  vorbis_info_clear(&vi2);

  vorbis_block_clear(&vb);
  vorbis_dsp_clear(&vd);
  ogg_stream_clear(&os);
  vorbis_comment_clear(&vc);
  vorbis_info_clear(&vi);
  ogg_sync_clear(&oy);
  if (failures != 0) return 1;
  std::puts("libvorbis C++ consumer ok");
  return 0;
}
