/* Parses a DNS-SD response built byte by byte as a responder sends it (a PTR
 * answer plus SRV, A, AAAA and TXT additional records, with compressed
 * names), the way mdns_query_recv parses a received datagram; compares a
 * compressed name case-insensitively with mdns_string_equal; and checks that
 * a compression-pointer loop, a truncated record and a short A record are
 * rejected.  No sockets: the CI sandboxes may forbid them. */
#include <mdns.h>

#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(expr)                                                      \
  do {                                                                   \
    if (!(expr)) {                                                       \
      fprintf(stderr, "main.c:%d: check failed: %s\n", __LINE__, #expr); \
      ++failures;                                                        \
    }                                                                    \
  } while (0)

static uint32_t storage[128]; /* the library wants 32-bit aligned buffers */
static unsigned char* const packet = (unsigned char*)storage;
static size_t packet_size = 0;

static size_t put(const void* bytes, size_t n) {
  size_t at = packet_size;
  memcpy(packet + packet_size, bytes, n);
  packet_size += n;
  return at;
}

static void put16(unsigned v) {
  packet[packet_size++] = (unsigned char)(v >> 8);
  packet[packet_size++] = (unsigned char)v;
}

static void put32(unsigned long v) {
  put16((unsigned)(v >> 16));
  put16((unsigned)(v & 0xffff));
}

static void record_header(unsigned owner, unsigned type, unsigned rclass, unsigned long ttl,
                          unsigned length) {
  put16(0xC000 | owner); /* compression pointer to the owner name */
  put16(type);
  put16(rclass);
  put32(ttl);
  put16(length);
}

static size_t service_at, instance_at, host_at;

static void build_response(void) {
  static const unsigned char header[12] = {0, 0, 0x84, 0, 0, 0, 0, 1, 0, 0, 0, 4};
  static const unsigned char ipv4[4] = {192, 168, 1, 10};
  static const unsigned char ipv6[16] = {0xfe, 0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
  /* The middle entry has a control character in its key and is skipped. */
  static const char txt[] = "\012path=/docs\003\001=x\006secure\006empty=";
  put(header, sizeof header);

  service_at = put("\005_http\004_tcp\005local", 18); /* with the terminating 0 */
  put16(MDNS_RECORDTYPE_PTR);
  put16(MDNS_CLASS_IN);
  put32(4500);
  put16(6);
  instance_at = put("\003web", 4);
  put16(0xC000 | (unsigned)service_at);

  record_header((unsigned)instance_at, MDNS_RECORDTYPE_SRV, MDNS_CACHE_FLUSH | MDNS_CLASS_IN, 120,
                13);
  put16(0);    /* priority */
  put16(5);    /* weight */
  put16(8080); /* port */
  host_at = put("\004host", 5);
  put16(0xC000 | (unsigned)(service_at + 11)); /* "local" */

  record_header((unsigned)host_at, MDNS_RECORDTYPE_A, MDNS_CACHE_FLUSH | MDNS_CLASS_IN, 120, 4);
  put(ipv4, sizeof ipv4);
  record_header((unsigned)host_at, MDNS_RECORDTYPE_AAAA, MDNS_CACHE_FLUSH | MDNS_CLASS_IN, 120,
                16);
  put(ipv6, sizeof ipv6);
  record_header((unsigned)instance_at, MDNS_RECORDTYPE_TXT, MDNS_CACHE_FLUSH | MDNS_CLASS_IN, 4500,
                sizeof txt - 1);
  put(txt, sizeof txt - 1);
}

static int is(mdns_string_t s, const char* text) {
  return s.length == strlen(text) && memcmp(s.str, text, s.length) == 0;
}

struct seen {
  int ptr, srv, a, aaaa, txt;
};

static int on_record(int sock, const struct sockaddr* from, size_t addrlen, mdns_entry_type_t entry,
                     uint16_t query_id, uint16_t rtype, uint16_t rclass, uint32_t ttl,
                     const void* data, size_t size, size_t name_offset, size_t name_length,
                     size_t record_offset, size_t record_length, void* user_data) {
  struct seen* seen = (struct seen*)user_data;
  char name[128];
  char target[128];
  size_t offset = name_offset;
  mdns_string_t owner = mdns_string_extract(data, size, &offset, name, sizeof name);
  (void)sock;
  (void)from;
  (void)addrlen;
  CHECK(query_id == 0);
  CHECK(offset == name_offset + name_length);

  switch (rtype) {
    case MDNS_RECORDTYPE_PTR: {
      mdns_string_t instance =
          mdns_record_parse_ptr(data, size, record_offset, record_length, target, sizeof target);
      ++seen->ptr;
      CHECK(entry == MDNS_ENTRYTYPE_ANSWER && rclass == MDNS_CLASS_IN && ttl == 4500);
      CHECK(is(owner, "_http._tcp.local.") && name_length == 18);
      CHECK(is(instance, "web._http._tcp.local."));
      break;
    }
    case MDNS_RECORDTYPE_SRV: {
      mdns_record_srv_t srv =
          mdns_record_parse_srv(data, size, record_offset, record_length, target, sizeof target);
      ++seen->srv;
      CHECK(entry == MDNS_ENTRYTYPE_ADDITIONAL && (rclass & MDNS_CACHE_FLUSH) &&
            (rclass & 0x7fff) == MDNS_CLASS_IN && ttl == 120);
      CHECK(is(owner, "web._http._tcp.local.") && name_length == 2);
      CHECK(srv.priority == 0 && srv.weight == 5 && srv.port == 8080);
      CHECK(is(srv.name, "host.local."));
      break;
    }
    case MDNS_RECORDTYPE_A: {
      struct sockaddr_in addr;
      const unsigned char* bytes = (const unsigned char*)&addr.sin_addr;
      mdns_record_parse_a(data, size, record_offset, record_length, &addr);
      ++seen->a;
      CHECK(is(owner, "host.local."));
      CHECK(addr.sin_family == AF_INET && bytes[0] == 192 && bytes[1] == 168 && bytes[2] == 1 &&
            bytes[3] == 10);
      break;
    }
    case MDNS_RECORDTYPE_AAAA: {
      struct sockaddr_in6 addr;
      const unsigned char* bytes = (const unsigned char*)&addr.sin6_addr;
      int k;
      mdns_record_parse_aaaa(data, size, record_offset, record_length, &addr);
      ++seen->aaaa;
      CHECK(is(owner, "host.local.") && addr.sin6_family == AF_INET6);
      CHECK(bytes[0] == 0xfe && bytes[1] == 0x80 && bytes[15] == 1);
      for (k = 2; k < 15; ++k) CHECK(bytes[k] == 0);
      break;
    }
    case MDNS_RECORDTYPE_TXT: {
      mdns_record_txt_t pairs[8];
      size_t count = mdns_record_parse_txt(data, size, record_offset, record_length, pairs, 8);
      ++seen->txt;
      CHECK(is(owner, "web._http._tcp.local.") && ttl == 4500);
      CHECK(count == 3);
      if (count != 3) break;
      CHECK(is(pairs[0].key, "path") && is(pairs[0].value, "/docs"));
      CHECK(is(pairs[1].key, "secure") && pairs[1].value.str == NULL &&
            pairs[1].value.length == 0);
      CHECK(is(pairs[2].key, "empty") && pairs[2].value.str != NULL &&
            pairs[2].value.length == 0);
      break;
    }
    default:
      CHECK(!"unexpected record type");
  }
  return 0;
}

static void parse_response(void) {
  struct seen seen = {0, 0, 0, 0, 0};
  size_t offset = 12;
  /* The header: no questions, one answer, four additional records. */
  CHECK(mdns_records_parse(0, NULL, 0, packet, packet_size, &offset, MDNS_ENTRYTYPE_ANSWER, 0, 1,
                           on_record, &seen) == 1);
  CHECK(mdns_records_parse(0, NULL, 0, packet, packet_size, &offset, MDNS_ENTRYTYPE_ADDITIONAL, 0,
                           4, on_record, &seen) == 4);
  CHECK(offset == packet_size);
  CHECK(seen.ptr == 1 && seen.srv == 1 && seen.a == 1 && seen.aaaa == 1 && seen.txt == 1);
}

static void compare_names(void) {
  static const char same[] = "\003WEB\005_HTTP\004_TCP\005LOCAL";
  static const char other[] = "\003web\005_http\004_udp\005local";
  size_t lhs = instance_at;
  size_t rhs = 0;
  /* Equal: both offsets move past the name, the left one past its pointer. */
  CHECK(mdns_string_equal(packet, packet_size, &lhs, same, sizeof same, &rhs));
  CHECK(lhs == instance_at + 6 && rhs == sizeof same);
  lhs = instance_at;
  rhs = 0;
  CHECK(!mdns_string_equal(packet, packet_size, &lhs, other, sizeof other, &rhs));
  CHECK(lhs == instance_at && rhs == 0);
}

static void malformed(void) {
  /* A name that points at itself. */
  static const unsigned char loop[14] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xC0, 12};
  char name[64];
  size_t offset = 12;
  struct seen seen = {0, 0, 0, 0, 0};
  struct sockaddr_in addr;
  mdns_string_t got = mdns_string_extract(loop, sizeof loop, &offset, name, sizeof name);
  CHECK(got.length == 0 && offset == 12);
  CHECK(!mdns_string_skip(loop, sizeof loop, &offset));

  /* One byte short: the TXT record's data no longer fits, so only the three
   * additional records before it are reported. */
  offset = 12;
  CHECK(mdns_records_parse(0, NULL, 0, packet, packet_size - 1, &offset, MDNS_ENTRYTYPE_ANSWER, 0,
                           1, on_record, &seen) == 1);
  CHECK(mdns_records_parse(0, NULL, 0, packet, packet_size - 1, &offset,
                           MDNS_ENTRYTYPE_ADDITIONAL, 0, 4, on_record, &seen) == 3);
  CHECK(seen.aaaa == 1 && seen.txt == 0);

  /* An A record must hold exactly four bytes. */
  mdns_record_parse_a(packet, packet_size, 0, 3, &addr);
  CHECK(addr.sin_family == AF_INET && addr.sin_addr.s_addr == 0);
}

int main(void) {
  build_response();
  parse_response();
  compare_names();
  malformed();
  printf("mdns (C): %s\n", failures == 0 ? "ok" : "FAILED");
  return failures == 0 ? 0 : 1;
}
