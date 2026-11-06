// C++ consumer: MSb-first bit packing, and a packet larger than one
// page's 255 lacing values, submitted through ogg_stream_iovecin, split
// across pages and reassembled through the sync layer.
#include <ogg/ogg.h>

#include <cstdio>
#include <cstring>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

void test_bitpack_msb() {
  const unsigned char expected[7] = {0xbf, 0xab, 0xc1, 0x23, 0x45, 0x67, 0x80};
  oggpack_buffer w;
  oggpackB_writeinit(&w);
  oggpackB_write(&w, 5, 3);
  oggpackB_write(&w, 0x1f, 5);
  oggpackB_write(&w, 0xabc, 12);
  oggpackB_write(&w, 0x12345678UL, 32);
  check(oggpackB_bits(&w) == 52 && oggpackB_bytes(&w) == 7, "MSb-first size");
  check(std::memcmp(oggpackB_get_buffer(&w), expected, 7) == 0,
        "MSb-first bytes");

  oggpack_buffer r;
  oggpackB_readinit(&r, oggpackB_get_buffer(&w), oggpackB_bytes(&w));
  check(oggpackB_look(&r, 8) == 0xbf, "oggpackB_look");
  oggpackB_adv(&r, 8);
  check(oggpackB_read(&r, 12) == 0xabc, "read 12 bits");
  check(oggpackB_read(&r, 32) == 0x12345678L, "read 32 bits");
  oggpackB_writeclear(&w);
}

void test_spanning_packet() {
  const int serial = 7;
  const long size = 70000;  // 274 lacing values of 255 plus one of 130
  std::vector<unsigned char> data(size);
  for (long i = 0; i < size; ++i) data[i] = static_cast<unsigned char>(i % 251);

  ogg_stream_state os;
  check(ogg_stream_init(&os, serial) == 0, "ogg_stream_init");
  unsigned char head[] = {'h', 'e', 'a', 'd'};
  ogg_packet op = {};
  op.packet = head;
  op.bytes = 4;
  op.b_o_s = 1;
  check(ogg_stream_packetin(&os, &op) == 0, "packetin head");

  ogg_iovec_t iov[2] = {{data.data(), 30000}, {data.data() + 30000, 40000}};
  check(ogg_stream_iovecin(&os, iov, 2, 1, 99) == 0, "ogg_stream_iovecin");

  std::vector<unsigned char> bytes;
  ogg_page og;
  int page = 0;
  while (ogg_stream_flush(&os, &og) == 1) {
    check(ogg_page_pageno(&og) == page, "page sequence number");
    check(ogg_page_serialno(&og) == serial, "page serialno");
    switch (page) {
      case 0:
        check(ogg_page_bos(&og) && ogg_page_packets(&og) == 1, "head page");
        break;
      case 1:  // a full segment table, and no packet ends here
        check(og.header_len == 27 + 255 && og.body_len == 255L * 255 &&
                  ogg_page_granulepos(&og) == -1 &&
                  ogg_page_packets(&og) == 0 && !ogg_page_continued(&og),
              "first data page");
        break;
      case 2:  // the remaining 19 * 255 + 130 bytes
        check(og.header_len == 27 + 20 && og.body_len == 19L * 255 + 130 &&
                  ogg_page_granulepos(&og) == 99 &&
                  ogg_page_continued(&og) && ogg_page_eos(&og) &&
                  ogg_page_packets(&og) == 1,
              "last data page");
        break;
    }
    bytes.insert(bytes.end(), og.header, og.header + og.header_len);
    bytes.insert(bytes.end(), og.body, og.body + og.body_len);
    ++page;
  }
  check(page == 3, "three pages");
  ogg_stream_clear(&os);

  ogg_sync_state oy;
  ogg_sync_init(&oy);
  char* buf = ogg_sync_buffer(&oy, static_cast<long>(bytes.size()));
  std::memcpy(buf, bytes.data(), bytes.size());
  ogg_sync_wrote(&oy, static_cast<long>(bytes.size()));

  ogg_stream_state is;
  check(ogg_stream_init(&is, serial) == 0, "decoder init");
  int pages_in = 0;
  while (ogg_sync_pageout(&oy, &og) == 1) {
    check(ogg_stream_pagein(&is, &og) == 0, "pagein");
    ++pages_in;
  }
  check(pages_in == 3, "sync returned three pages");

  check(ogg_stream_packetout(&is, &op) == 1 && op.bytes == 4 &&
            std::memcmp(op.packet, head, 4) == 0 && op.b_o_s,
        "head packet");
  ogg_packet peek;
  check(ogg_stream_packetpeek(&is, &peek) == 1 && peek.bytes == size,
        "ogg_stream_packetpeek");
  check(ogg_stream_packetout(&is, &op) == 1 && op.bytes == size &&
            std::memcmp(op.packet, data.data(), size) == 0 && op.e_o_s &&
            op.granulepos == 99 && op.packetno == 1,
        "spanning packet");
  check(ogg_stream_packetout(&is, &op) == 0, "no more packets");
  ogg_stream_clear(&is);
  ogg_sync_clear(&oy);
}

}  // namespace

int main() {
  test_bitpack_msb();
  test_spanning_packet();
  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("libogg C++ consumer ok");
  return 0;
}
