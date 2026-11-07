/* C consumer: integer types, LSb-first bit packing, and an encode/decode
 * round trip whose page bytes are checked against RFC 3533 framing with
 * CRCs computed independently of libogg. */
#include <ogg/ogg.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

static void check(int ok, const char *what) {
  if (!ok) {
    fprintf(stderr, "FAIL: %s\n", what);
    failures++;
  }
}

/* os_types.h (via config_types.h on Linux) must give exact widths. */
static void test_types(void) {
  check(sizeof(ogg_int16_t) == 2 && sizeof(ogg_uint16_t) == 2, "16-bit types");
  check(sizeof(ogg_int32_t) == 4 && sizeof(ogg_uint32_t) == 4, "32-bit types");
  check(sizeof(ogg_int64_t) == 8 && sizeof(ogg_uint64_t) == 8, "64-bit types");
  check((ogg_int16_t)-1 < 0 && (ogg_int32_t)-1 < 0 && (ogg_int64_t)-1 < 0,
        "signed types are signed");
  check((ogg_uint16_t)-1 > 0 && (ogg_uint32_t)-1 > 0 && (ogg_uint64_t)-1 > 0,
        "unsigned types are unsigned");
}

static void test_bitpack(void) {
  static const unsigned char expected[7] = {0xfd, 0xbc, 0x8a, 0x67,
                                            0x45, 0x23, 0x01};
  oggpack_buffer w, r;
  oggpack_writeinit(&w);
  oggpack_write(&w, 5, 3);
  oggpack_write(&w, 0x1f, 5);
  oggpack_write(&w, 0xabc, 12);
  oggpack_write(&w, 0x12345678UL, 32);
  check(oggpack_bits(&w) == 52, "oggpack_bits");
  check(oggpack_bytes(&w) == 7, "oggpack_bytes");
  check(memcmp(oggpack_get_buffer(&w), expected, 7) == 0, "LSb-first bytes");

  oggpack_readinit(&r, oggpack_get_buffer(&w), oggpack_bytes(&w));
  check(oggpack_look(&r, 3) == 5, "oggpack_look");
  check(oggpack_read(&r, 3) == 5, "read 3 bits");
  check(oggpack_read(&r, 5) == 0x1f, "read 5 bits");
  check(oggpack_read(&r, 12) == 0xabc, "read 12 bits");
  check(oggpack_read(&r, 32) == 0x12345678L, "read 32 bits");
  /* Only 4 padding bits remain, so a 32-bit read must report underflow. */
  check(oggpack_read(&r, 32) == -1, "read past end");
  oggpack_writeclear(&w);
}

#define SERIAL 0x12345678

/* RFC 3533 headers with their CRC fields; see the stream in test_framing. */
static const unsigned char page0_header[28] = {
    0x4f, 0x67, 0x67, 0x53, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x78, 0x56, 0x34, 0x12, 0x00, 0x00,
    0x00, 0x00, 0xdd, 0xb6, 0x79, 0xc4, 0x01, 0x0c};
static const unsigned char page1_header[30] = {
    0x4f, 0x67, 0x67, 0x53, 0x00, 0x04, 0xcb, 0x04, 0xfb, 0x71,
    0x1f, 0x01, 0x00, 0x00, 0x78, 0x56, 0x34, 0x12, 0x01, 0x00,
    0x00, 0x00, 0xf0, 0x2b, 0xd5, 0x21, 0x03, 0xff, 0x2d, 0x03};

static unsigned char stream_bytes[4096];
static long stream_len = 0;

static void append_page(const ogg_page *og) {
  memcpy(stream_bytes + stream_len, og->header, (size_t)og->header_len);
  stream_len += og->header_len;
  memcpy(stream_bytes + stream_len, og->body, (size_t)og->body_len);
  stream_len += og->body_len;
}

/* Feeds `len` bytes to a fresh sync state in 7-byte chunks, copies each
 * page it accepts into `pages` (at most `max_pages`; the caller frees
 * each header), and returns the number accepted, counting sync losses in
 * *holes. */
static int sync_pages(const unsigned char *data, long len, ogg_page *pages,
                      int max_pages, int *holes) {
  ogg_sync_state oy;
  ogg_page og;
  int n = 0;
  long off;
  ogg_sync_init(&oy);
  *holes = 0;
  for (off = 0; off < len; off += 7) {
    long chunk = len - off < 7 ? len - off : 7;
    char *buf = ogg_sync_buffer(&oy, chunk);
    int ret;
    memcpy(buf, data + off, (size_t)chunk);
    ogg_sync_wrote(&oy, chunk);
    while ((ret = ogg_sync_pageout(&oy, &og)) != 0) {
      unsigned char *copy;
      if (ret < 0) {
        (*holes)++;
        continue;
      }
      if (n == max_pages) break;
      /* og points into oy's buffer, which later writes may move. */
      copy = (unsigned char *)malloc((size_t)(og.header_len + og.body_len));
      memcpy(copy, og.header, (size_t)og.header_len);
      memcpy(copy + og.header_len, og.body, (size_t)og.body_len);
      pages[n].header = copy;
      pages[n].header_len = og.header_len;
      pages[n].body = copy + og.header_len;
      pages[n].body_len = og.body_len;
      n++;
    }
  }
  ogg_sync_clear(&oy);
  return n;
}

static void test_framing(void) {
  static const unsigned char a[] = "libogg-cabin";
  static unsigned char b[300];
  static const unsigned char c[] = "end";
  ogg_stream_state os, is;
  ogg_packet op;
  ogg_page og, pages[4];
  int i, n, holes;

  for (i = 0; i < 300; i++) b[i] = (unsigned char)(i * 7 + 3);

  check(ogg_stream_init(&os, SERIAL) == 0, "ogg_stream_init");
  check(ogg_stream_check(&os) == 0, "ogg_stream_check");

  op.packet = (unsigned char *)a;
  op.bytes = 12;
  op.b_o_s = 1;
  op.e_o_s = 0;
  op.granulepos = 0;
  op.packetno = 0;
  check(ogg_stream_packetin(&os, &op) == 0, "packetin a");
  op.packet = b;
  op.bytes = 300;
  op.b_o_s = 0;
  op.granulepos = 1000;
  op.packetno = 1;
  check(ogg_stream_packetin(&os, &op) == 0, "packetin b");
  op.packet = (unsigned char *)c;
  op.bytes = 3;
  op.e_o_s = 1;
  op.granulepos = 1234567890123LL;
  op.packetno = 2;
  check(ogg_stream_packetin(&os, &op) == 0, "packetin c");

  /* The first page carries only the initial header packet. */
  check(ogg_stream_flush(&os, &og) == 1, "flush page 0");
  check(og.header_len == 28 && memcmp(og.header, page0_header, 28) == 0,
        "page 0 header bytes");
  check(og.body_len == 12 && memcmp(og.body, a, 12) == 0, "page 0 body");
  check(ogg_page_bos(&og) && !ogg_page_eos(&og) && ogg_page_pageno(&og) == 0,
        "page 0 flags");
  append_page(&og);

  check(ogg_stream_flush(&os, &og) == 1, "flush page 1");
  check(og.header_len == 30 && memcmp(og.header, page1_header, 30) == 0,
        "page 1 header bytes");
  check(og.body_len == 303 && memcmp(og.body, b, 300) == 0 &&
            memcmp(og.body + 300, c, 3) == 0,
        "page 1 body");
  check(ogg_page_granulepos(&og) == 1234567890123LL, "page 1 granulepos");
  check(ogg_page_serialno(&og) == SERIAL, "page 1 serialno");
  check(ogg_page_packets(&og) == 2 && ogg_page_eos(&og) &&
            !ogg_page_continued(&og),
        "page 1 packets and flags");
  /* Recomputing the CRC must reproduce the independently computed one. */
  og.header[22] = og.header[23] = og.header[24] = og.header[25] = 0;
  ogg_page_checksum_set(&og);
  check(memcmp(og.header + 22, page1_header + 22, 4) == 0,
        "ogg_page_checksum_set");
  append_page(&og);
  check(ogg_stream_flush(&os, &og) == 0, "stream drained");
  ogg_stream_clear(&os);

  /* Decode the byte stream back into the three packets. */
  n = sync_pages(stream_bytes, stream_len, pages, 4, &holes);
  check(n == 2 && holes == 0, "sync found both pages");
  if (n != 2) return;
  check(ogg_stream_init(&is, ogg_page_serialno(&pages[0])) == 0,
        "decoder init");
  for (i = 0; i < 2; i++) {
    check(ogg_stream_pagein(&is, &pages[i]) == 0, "pagein");
    free(pages[i].header);
  }
  check(ogg_stream_packetout(&is, &op) == 1 && op.bytes == 12 &&
            memcmp(op.packet, a, 12) == 0 && op.b_o_s && !op.e_o_s &&
            op.packetno == 0,
        "packet a");
  check(ogg_stream_packetout(&is, &op) == 1 && op.bytes == 300 &&
            memcmp(op.packet, b, 300) == 0 && op.packetno == 1,
        "packet b");
  check(ogg_stream_packetout(&is, &op) == 1 && op.bytes == 3 &&
            memcmp(op.packet, c, 3) == 0 && op.e_o_s &&
            op.granulepos == 1234567890123LL && op.packetno == 2,
        "packet c");
  check(ogg_stream_packetout(&is, &op) == 0, "no more packets");
  ogg_stream_clear(&is);

  /* A corrupted body byte must fail the page CRC: the sync layer drops
   * the page and reports the hole. */
  stream_bytes[28 + 12 + 30 + 100] ^= 0x01;
  n = sync_pages(stream_bytes, stream_len, pages, 4, &holes);
  check(n == 1 && holes >= 1, "corrupted page rejected");
  for (i = 0; i < n; i++) free(pages[i].header);
}

int main(void) {
  test_types();
  test_bitpack();
  test_framing();
  if (failures) {
    fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  puts("libogg C consumer ok");
  return 0;
}
