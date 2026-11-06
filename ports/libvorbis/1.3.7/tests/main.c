/* Encodes one second of stereo sine tones with vorbisenc's VBR mode into
 * an in-memory Ogg stream, then decodes and seeks it with vorbisfile. */
#include <vorbis/codec.h>
#include <vorbis/vorbisenc.h>
#include <vorbis/vorbisfile.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RATE 44100
#define FRAMES 44100
#define TWO_PI 6.283185307179586

static int failures;

static void check(int ok, const char *what) {
  if (!ok) {
    fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

/* Left 440 Hz, right 660 Hz, so a channel swap shows up as noise. */
static float source(int channel, long i) {
  double hz = channel == 0 ? 440.0 : 660.0;
  return (float)(0.5 * sin(TWO_PI * hz * (double)i / RATE));
}

struct buffer {
  unsigned char *data;
  size_t size;
  size_t pos;
};

static void append(struct buffer *b, const unsigned char *p, long n) {
  b->data = (unsigned char *)realloc(b->data, b->size + (size_t)n);
  memcpy(b->data + b->size, p, (size_t)n);
  b->size += (size_t)n;
}

static void append_page(struct buffer *b, const ogg_page *og) {
  append(b, og->header, og->header_len);
  append(b, og->body, og->body_len);
}

static void encode(struct buffer *out) {
  vorbis_info vi;
  vorbis_comment vc;
  vorbis_dsp_state vd;
  vorbis_block vb;
  ogg_stream_state os;
  ogg_packet header, comment, codebooks, op;
  ogg_page og;
  long pos = 0;
  int eos = 0;

  vorbis_info_init(&vi);
  check(vorbis_encode_init_vbr(&vi, 2, RATE, 0.4f) == 0, "encode_init_vbr");
  check(vi.channels == 2 && vi.rate == RATE, "encoder info");
  vorbis_comment_init(&vc);
  vorbis_comment_add_tag(&vc, "TITLE", "cabin");
  vorbis_analysis_init(&vd, &vi);
  vorbis_block_init(&vd, &vb);
  ogg_stream_init(&os, 1234);

  vorbis_analysis_headerout(&vd, &vc, &header, &comment, &codebooks);
  check(header.b_o_s == 1 && header.bytes == 30, "identification header");
  ogg_stream_packetin(&os, &header);
  ogg_stream_packetin(&os, &comment);
  ogg_stream_packetin(&os, &codebooks);
  while (ogg_stream_flush(&os, &og) != 0) append_page(out, &og);

  while (!eos) {
    if (pos < FRAMES) {
      int n = FRAMES - pos < 1000 ? (int)(FRAMES - pos) : 1000;
      float **in = vorbis_analysis_buffer(&vd, n);
      int i, c;
      for (c = 0; c < 2; ++c)
        for (i = 0; i < n; ++i) in[c][i] = source(c, pos + i);
      vorbis_analysis_wrote(&vd, n);
      pos += n;
    } else {
      vorbis_analysis_wrote(&vd, 0);
    }
    while (vorbis_analysis_blockout(&vd, &vb) == 1) {
      vorbis_analysis(&vb, NULL);
      vorbis_bitrate_addblock(&vb);
      while (vorbis_bitrate_flushpacket(&vd, &op)) {
        ogg_stream_packetin(&os, &op);
        while (ogg_stream_pageout(&os, &og) != 0) {
          append_page(out, &og);
          if (ogg_page_eos(&og)) eos = 1;
        }
      }
    }
  }

  ogg_stream_clear(&os);
  vorbis_block_clear(&vb);
  vorbis_dsp_clear(&vd);
  vorbis_comment_clear(&vc);
  vorbis_info_clear(&vi);
}

static size_t mem_read(void *ptr, size_t size, size_t nmemb, void *src) {
  struct buffer *b = (struct buffer *)src;
  size_t n = size * nmemb;
  if (n > b->size - b->pos) n = b->size - b->pos;
  memcpy(ptr, b->data + b->pos, n);
  b->pos += n;
  return n / size;
}

static int mem_seek(void *src, ogg_int64_t offset, int whence) {
  struct buffer *b = (struct buffer *)src;
  ogg_int64_t base = whence == SEEK_SET ? 0
                     : whence == SEEK_CUR ? (ogg_int64_t)b->pos
                                          : (ogg_int64_t)b->size;
  if (base + offset < 0 || base + offset > (ogg_int64_t)b->size) return -1;
  b->pos = (size_t)(base + offset);
  return 0;
}

static long mem_tell(void *src) { return (long)((struct buffer *)src)->pos; }

/* Reads interleaved little-endian signed 16-bit stereo through ov_read. */
static long read_s16(OggVorbis_File *vf, short *pcm, long frames) {
  long got = 0;
  while (got < frames) {
    unsigned char bytes[4096];
    int section;
    long want = (frames - got) * 4, n, i;
    if (want > (long)sizeof bytes) want = (long)sizeof bytes;
    n = ov_read(vf, (char *)bytes, (int)want, 0, 2, 1, &section);
    if (n <= 0) break;
    for (i = 0; i < n / 2; ++i)
      pcm[got * 2 + i] = (short)(bytes[2 * i] | (bytes[2 * i + 1] << 8));
    got += n / 4;
  }
  return got;
}

/* Signal-to-noise ratio, in dB, of a decoded channel against source(). */
static double snr_db(const short *pcm, int channel, long start, long frames) {
  double signal = 0, noise = 0;
  long i;
  for (i = 0; i < frames; ++i) {
    double want = source(channel, start + i);
    double got = pcm[i * 2 + channel] / 32768.0;
    signal += want * want;
    noise += (got - want) * (got - want);
  }
  return 10 * log10(signal / noise);
}

int main(void) {
  struct buffer stream = {NULL, 0, 0};
  ov_callbacks callbacks = {mem_read, mem_seek, NULL, mem_tell};
  OggVorbis_File vf;
  vorbis_info *vi;
  vorbis_comment *vc;
  const char *title;
  short *pcm = (short *)malloc(sizeof(short) * 2 * (FRAMES + 1));
  long frames;
  struct buffer junk = {NULL, 0, 0};
  static const unsigned char not_vorbis[64] = "OggS but not really";

  check(strcmp(vorbis_version_string(), "Xiph.Org libVorbis 1.3.7") == 0,
        "version string");

  encode(&stream);
  check(stream.size > 1000 && stream.size < 100000, "stream size");
  check(memcmp(stream.data, "OggS", 4) == 0, "Ogg capture pattern");

  if (ov_open_callbacks(&stream, &vf, NULL, 0, callbacks) != 0) {
    fputs("FAIL: ov_open_callbacks\n", stderr);
    return 1;
  }
  check(ov_seekable(&vf) == 1 && ov_streams(&vf) == 1, "one seekable stream");
  vi = ov_info(&vf, -1);
  check(vi->channels == 2 && vi->rate == RATE, "decoded info");
  check(ov_pcm_total(&vf, -1) == FRAMES, "ov_pcm_total");
  check(fabs(ov_time_total(&vf, -1) - 1.0) < 1e-9, "ov_time_total");
  check(ov_serialnumber(&vf, -1) == 1234, "serial number");
  vc = ov_comment(&vf, -1);
  check(strcmp(vc->vendor,
               "Xiph.Org libVorbis I 20200704 (Reducing Environment)") == 0,
        "vendor");
  check(vc->comments == 1, "comment count");
  check(vorbis_comment_query_count(vc, "title") == 1, "comment tag count");
  title = vorbis_comment_query(vc, "TITLE", 0);
  check(title != NULL && strcmp(title, "cabin") == 0, "comment value");

  frames = read_s16(&vf, pcm, FRAMES + 1);
  check(frames == FRAMES, "decoded length");
  check(snr_db(pcm, 0, 0, FRAMES) > 20, "left channel SNR");
  check(snr_db(pcm, 1, 0, FRAMES) > 20, "right channel SNR");

  check(ov_pcm_seek(&vf, 30000) == 0, "ov_pcm_seek");
  check(ov_pcm_tell(&vf) == 30000, "ov_pcm_tell");
  check(read_s16(&vf, pcm, 1000) == 1000, "read after seek");
  check(snr_db(pcm, 0, 30000, 1000) > 20, "left SNR after seek");
  check(snr_db(pcm, 1, 30000, 1000) > 20, "right SNR after seek");
  ov_clear(&vf);

  append(&junk, not_vorbis, sizeof not_vorbis);
  check(ov_open_callbacks(&junk, &vf, NULL, 0, callbacks) == OV_ENOTVORBIS,
        "non-Vorbis data rejected");

  free(junk.data);
  free(stream.data);
  free(pcm);
  if (failures != 0) return 1;
  puts("libvorbis C consumer ok");
  return 0;
}
