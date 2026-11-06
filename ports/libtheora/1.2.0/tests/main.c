/* Encodes ten synthetic 64x48 4:2:0 frames with the th_encode API, then
 * decodes them with the th_decode API and compares the pictures. */
#include <theora/theoradec.h>
#include <theora/theoraenc.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 64
#define H 48
#define FRAMES 10
#define MAX_PACKETS 32

static int failures;

static void check(int ok, const char *what) {
  if (!ok) {
    fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

/* A smooth diagonal ramp that drifts, plus a bright square moving right;
 * integer arithmetic, so every platform encodes the same pictures. */
static unsigned char luma(int x, int y, int f) {
  int v = (x * 3 + y * 2 + f * 4) % 256;
  if (x >= 4 + f * 4 && x < 12 + f * 4 && y >= 20 && y < 28) return 235;
  return (unsigned char)(v < 128 ? 16 + v : 16 + (255 - v));
}

static unsigned char cb(int x) { return (unsigned char)(96 + x * 2); }
static unsigned char cr(int y) { return (unsigned char)(160 - y * 2); }

struct packet {
  unsigned char *data;
  ogg_packet op;
};

static struct packet packets[MAX_PACKETS];
static int npackets;

static void keep(const ogg_packet *op) {
  struct packet *p = &packets[npackets++];
  p->data = (unsigned char *)malloc((size_t)op->bytes + 1);
  memcpy(p->data, op->packet, (size_t)op->bytes);
  p->op = *op;
  p->op.packet = p->data;
}

static void fill(unsigned char *y, unsigned char *u, unsigned char *v, int f) {
  int i, j;
  for (j = 0; j < H; ++j)
    for (i = 0; i < W; ++i) y[j * W + i] = luma(i, j, f);
  for (j = 0; j < H / 2; ++j)
    for (i = 0; i < W / 2; ++i) {
      u[j * (W / 2) + i] = cb(i);
      v[j * (W / 2) + i] = cr(j);
    }
}

static void base_info(th_info *ti) {
  th_info_init(ti);
  ti->frame_width = W;
  ti->frame_height = H;
  ti->pic_width = W;
  ti->pic_height = H;
  ti->pic_x = 0;
  ti->pic_y = 0;
  ti->fps_numerator = 25;
  ti->fps_denominator = 1;
  ti->aspect_numerator = 1;
  ti->aspect_denominator = 1;
  ti->colorspace = TH_CS_UNSPECIFIED;
  ti->pixel_fmt = TH_PF_420;
  ti->target_bitrate = 0;
  ti->quality = 48;
  ti->keyframe_granule_shift = 6;
}

static void encode(void) {
  th_info ti;
  th_comment tc;
  th_enc_ctx *enc;
  ogg_packet op;
  ogg_uint32_t keyframe_every = 4;
  unsigned char y[W * H], u[W * H / 4], v[W * H / 4];
  th_ycbcr_buffer buf;
  int f, r;

  base_info(&ti);
  enc = th_encode_alloc(&ti);
  th_info_clear(&ti);
  check(enc != NULL, "th_encode_alloc");
  if (enc == NULL) return;
  check(th_encode_ctl(enc, TH_ENCCTL_SET_KEYFRAME_FREQUENCY_FORCE,
                      &keyframe_every, sizeof keyframe_every) == 0,
        "keyframe frequency");
  check(keyframe_every == 4, "keyframe frequency accepted");

  th_comment_init(&tc);
  th_comment_add_tag(&tc, "TITLE", "cabin");
  while ((r = th_encode_flushheader(enc, &tc, &op)) > 0) keep(&op);
  check(r == 0 && npackets == 3, "three header packets");
  check(packets[0].op.b_o_s == 1, "first header begins the stream");
  th_comment_clear(&tc);

  buf[0].width = W;
  buf[0].height = H;
  buf[0].stride = W;
  buf[0].data = y;
  buf[1].width = buf[2].width = W / 2;
  buf[1].height = buf[2].height = H / 2;
  buf[1].stride = buf[2].stride = W / 2;
  buf[1].data = u;
  buf[2].data = v;
  for (f = 0; f < FRAMES; ++f) {
    fill(y, u, v, f);
    check(th_encode_ycbcr_in(enc, buf) == 0, "th_encode_ycbcr_in");
    while (th_encode_packetout(enc, f == FRAMES - 1, &op) > 0) keep(&op);
  }
  check(npackets == 3 + FRAMES, "one packet per frame");
  check(packets[npackets - 1].op.e_o_s == 1, "last packet ends the stream");
  th_encode_free(enc);
}

/* FNV-1a over every packet.  The encoder uses integer arithmetic only, and
 * its x86 SIMD routines give the C ones' results wherever the bitstream
 * depends on them, so every platform should emit these bytes; the value is
 * the C code's (macOS arm64). */
static unsigned long stream_hash(void) {
  unsigned long h = 2166136261UL;
  int i;
  long j;
  for (i = 0; i < npackets; ++i)
    for (j = 0; j < packets[i].op.bytes; ++j)
      h = ((h ^ packets[i].data[j]) * 16777619UL) & 0xffffffffUL;
  return h;
}

/* PSNR above 30 dB, i.e. 255^2 * n / se > 10^3, without <math.h>: libm is
 * a separate library on Linux, and libtheora itself needs none. */
static int above_30db(const th_img_plane *p, int f, int plane) {
  double se = 0;
  int i, j, n = 0;
  for (j = 0; j < p->height; ++j)
    for (i = 0; i < p->width; ++i) {
      int want = plane == 0 ? luma(i, j, f) : plane == 1 ? cb(i) : cr(j);
      int d = p->data[j * p->stride + i] - want;
      se += d * d;
      ++n;
    }
  return n > 0 && 1000.0 * se < 65025.0 * n;
}

static void decode(void) {
  th_info ti;
  th_comment tc;
  th_setup_info *setup = NULL;
  th_dec_ctx *dec;
  th_ycbcr_buffer out;
  const char *title;
  int i, r;

  th_info_init(&ti);
  th_comment_init(&tc);
  for (i = 0; i < 3; ++i) {
    r = th_decode_headerin(&ti, &tc, &setup, &packets[i].op);
    check(r == 3 - i, "header packet");
  }
  check(th_decode_headerin(&ti, &tc, &setup, &packets[3].op) == 0,
        "first data packet ends the headers");
  check(ti.version_major == 3 && ti.version_minor == 2, "bitstream version");
  check(ti.frame_width == W && ti.frame_height == H, "frame size");
  check(ti.pic_width == W && ti.pic_height == H, "picture size");
  check(ti.fps_numerator == 25 && ti.fps_denominator == 1, "frame rate");
  check(ti.pixel_fmt == TH_PF_420, "pixel format");
  check(strcmp(tc.vendor, "Xiph.Org libtheora 1.2.0 20250329 (Ptalarbvorm)") == 0,
        "vendor");
  title = th_comment_query(&tc, "TITLE", 0);
  check(title != NULL && strcmp(title, "cabin") == 0, "comment");

  dec = th_decode_alloc(&ti, setup);
  th_setup_free(setup);
  check(dec != NULL, "th_decode_alloc");
  if (dec != NULL) {
    for (i = 3; i < npackets; ++i) {
      int f = i - 3;
      ogg_int64_t gp = -1;
      check(th_decode_packetin(dec, &packets[i].op, &gp) == 0,
            "th_decode_packetin");
      check(th_packet_iskeyframe(&packets[i].op) == (f % 4 == 0), "keyframes");
      check(th_granule_frame(dec, gp) == f, "granule frame");
      check(th_decode_ycbcr_out(dec, out) == 0, "th_decode_ycbcr_out");
      check(out[0].width == W && out[0].height == H, "decoded size");
      check(above_30db(&out[0], f, 0), "luma PSNR");
      check(above_30db(&out[1], f, 1) && above_30db(&out[2], f, 2),
            "chroma PSNR");
      if (f == FRAMES - 1) {
        double t = th_granule_time(dec, gp) - 0.4;
        check(t < 1e-9 && t > -1e-9, "granule time");
      }
    }
    th_decode_free(dec);
  }
  th_comment_clear(&tc);
  th_info_clear(&ti);
}

static void rejects(void) {
  th_info ti;
  th_comment tc;
  th_setup_info *setup = NULL;
  unsigned char not_theora[] = {0x80, 'n', 'o', 't', 'v', 'p', '3', 0, 0};
  ogg_packet op;

  base_info(&ti);
  ti.frame_width = ti.pic_width = 60; /* Not a multiple of 16. */
  check(th_encode_alloc(&ti) == NULL, "bad frame size rejected");
  th_info_clear(&ti);

  th_info_init(&ti);
  th_comment_init(&tc);
  memset(&op, 0, sizeof op);
  op.packet = not_theora;
  op.bytes = sizeof not_theora;
  op.b_o_s = 1;
  check(th_decode_headerin(&ti, &tc, &setup, &op) == TH_ENOTFORMAT,
        "non-Theora header rejected");
  /* The comment header before the identification header. */
  check(th_decode_headerin(&ti, &tc, &setup, &packets[1].op) == TH_EBADHEADER,
        "out-of-order header rejected");
  th_comment_clear(&tc);
  th_info_clear(&ti);
}

int main(void) {
  int i;
  check(strcmp(th_version_string(),
               "Xiph.Org libtheora 1.2.0 20250329 (Ptalarbvorm)") == 0,
        "version string");
  check(th_version_number() == 0x030201, "bitstream version number");

  encode();
  if (npackets == 3 + FRAMES) {
    check(stream_hash() == 0x932bfa50UL, "bitstream hash");
    decode();
    rejects();
  }
  for (i = 0; i < npackets; ++i) free(packets[i].data);
  if (failures != 0) return 1;
  puts("libtheora C consumer ok");
  return 0;
}
