#include <re.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#define CHECK(cond)                                                       \
  do {                                                                    \
    if (!(cond)) {                                                        \
      std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__,         \
                   __LINE__, #cond);                                      \
      return 1;                                                           \
    }                                                                     \
  } while (0)

namespace {

std::string to_string(const pl& p) { return std::string(p.p, p.l); }

int check_sip_uri() {
  pl in;
  pl_set_str(&in, "sip:alice@atlanta.com:5070;transport=tcp");
  uri u;
  CHECK(uri_decode(&u, &in) == 0);
  CHECK(to_string(u.scheme) == "sip");
  CHECK(to_string(u.user) == "alice");
  CHECK(to_string(u.host) == "atlanta.com");
  CHECK(u.port == 5070);
  CHECK(to_string(u.params) == ";transport=tcp");

  const char msg[] = "SIP/2.0 180 Ringing";
  pl ver, code, reason;
  CHECK(re_regex(msg, std::strlen(msg), "SIP/[0-9.]+ [0-9]+ [^]+", &ver,
                 &code, &reason) == 0);
  CHECK(to_string(ver) == "2.0");
  CHECK(pl_u32(&code) == 180);
  CHECK(pl_strcmp(&reason, "Ringing") == 0);
  return 0;
}

int check_json() {
  const char doc[] = "{\"name\":\"bob\",\"port\":5060,\"tags\":[1,2]}";
  odict* od = nullptr;
  CHECK(json_decode_odict(&od, 8, doc, std::strlen(doc), 4) == 0);
  const char* name = odict_string(od, "name");
  std::uint64_t port = 0;
  bool ok = name && std::strcmp(name, "bob") == 0 &&
            odict_get_number(od, &port, "port") && port == 5060 &&
            odict_get_type(od, ODICT_ARRAY, "tags") != nullptr;
  od = static_cast<odict*>(mem_deref(od));
  CHECK(ok);
  return 0;
}

int check_mbuf_and_h264() {
  mbuf* mb = mbuf_alloc(16);
  CHECK(mb != nullptr);
  bool ok = mbuf_write_u16(mb, htons(0x1234)) == 0 &&
            mbuf_printf(mb, "id=%u", 7u) == 0;
  mbuf_set_pos(mb, 0);
  ok = ok && ntohs(mbuf_read_u16(mb)) == 0x1234 &&
       mbuf_get_left(mb) == 4 &&
       std::memcmp(mbuf_buf(mb), "id=7", 4) == 0;
  mb = static_cast<mbuf*>(mem_deref(mb));
  CHECK(ok);

  // A 4-byte start code at offset 2, then a 3-byte one at offset 9.
  const std::uint8_t stream[] = {0xaa, 0xbb, 0x00, 0x00, 0x00, 0x01, 0x67,
                                 0x42, 0xc0, 0x00, 0x00, 0x01, 0x68, 0xce};
  const std::uint8_t* end = stream + sizeof(stream);
  const std::uint8_t* first = h264_find_startcode(stream, end);
  CHECK(first == stream + 2);
  const std::uint8_t* second = h264_find_startcode(first + 4, end);
  CHECK(second == stream + 9);
  CHECK(h264_find_startcode(second + 3, end) == end);
  return 0;
}

int check_md5() {
  std::uint8_t md[MD5_SIZE];
  md5(reinterpret_cast<const std::uint8_t*>("abc"), 3, md);
  char hex[MD5_STR_SIZE];
  CHECK(re_snprintf(hex, sizeof(hex), "%w", md, sizeof(md)) == 32);
  CHECK(std::strcmp(hex, "900150983cd24fb0d6963f7d28e17f72") == 0);
  return 0;
}

}  // namespace

int main() {
  CHECK(libre_init() == 0);
  int err = check_sip_uri() || check_json() || check_mbuf_and_h264() ||
            check_md5();
  libre_close();
  if (err)
    return 1;
  std::puts("baresip-libre C++ consumer: ok");
  return 0;
}
