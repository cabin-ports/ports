// Builds a DNS-SD response with the record writers mdns_query_answer_unicast
// and the multicast answers use (PTR, SRV, A and AAAA records, two TXT
// entries coalesced into one record), checking that the string table
// compresses repeated names, then parses the packet back with
// mdns_records_parse and the record parsers.  No sockets: the CI sandboxes
// may forbid them.
#include <mdns.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

int failures = 0;

void check(bool ok, int line, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "main.cc:%d: check failed: %s\n", line, what);
    ++failures;
  }
}

#define CHECK(expr) check((expr), __LINE__, #expr)

mdns_string_t text(const char* s) { return mdns_string_t{s, std::strlen(s)}; }

std::string str(mdns_string_t s) { return std::string(s.str, s.length); }

struct Seen {
  int ptr = 0, srv = 0, a = 0, aaaa = 0, txt = 0;
};

int on_record(int, const struct sockaddr*, size_t, mdns_entry_type_t entry, uint16_t query_id,
              uint16_t rtype, uint16_t rclass, uint32_t ttl, const void* data, size_t size,
              size_t name_offset, size_t name_length, size_t record_offset, size_t record_length,
              void* user_data) {
  Seen& seen = *static_cast<Seen*>(user_data);
  char name[128];
  char target[128];
  const std::string owner = str(mdns_string_extract(data, size, &name_offset, name, sizeof name));
  CHECK(query_id == 0x1234);
  CHECK(entry == (rtype == MDNS_RECORDTYPE_PTR ? MDNS_ENTRYTYPE_ANSWER : MDNS_ENTRYTYPE_ADDITIONAL));

  switch (rtype) {
    case MDNS_RECORDTYPE_PTR:
      ++seen.ptr;
      CHECK(owner == "_http._tcp.local." && name_length == 18 && rclass == MDNS_CLASS_IN &&
            ttl == 4500);
      CHECK(str(mdns_record_parse_ptr(data, size, record_offset, record_length, target,
                                      sizeof target)) == "web._http._tcp.local.");
      // "web" then a pointer to the owner name written just before.
      CHECK(record_length == 6);
      break;
    case MDNS_RECORDTYPE_SRV: {
      ++seen.srv;
      // The owner repeats the PTR target, so it is a two-byte pointer.
      CHECK(owner == "web._http._tcp.local." && name_length == 2);
      CHECK(rclass == (MDNS_CACHE_FLUSH | MDNS_CLASS_IN) && ttl == 120);
      const mdns_record_srv_t srv =
          mdns_record_parse_srv(data, size, record_offset, record_length, target, sizeof target);
      CHECK(srv.priority == 1 && srv.weight == 5 && srv.port == 8080);
      CHECK(str(srv.name) == "host.local.");
      CHECK(record_length == 6 + 5 + 2);  // "host", then a pointer to "local"
      break;
    }
    case MDNS_RECORDTYPE_A: {
      ++seen.a;
      CHECK(owner == "host.local." && name_length == 2);
      sockaddr_in addr;
      mdns_record_parse_a(data, size, record_offset, record_length, &addr);
      CHECK(addr.sin_family == AF_INET && addr.sin_addr.s_addr == htonl(0xC0A8010A));
      break;
    }
    case MDNS_RECORDTYPE_AAAA: {
      ++seen.aaaa;
      CHECK(owner == "host.local." && name_length == 2);
      sockaddr_in6 addr;
      mdns_record_parse_aaaa(data, size, record_offset, record_length, &addr);
      const unsigned char* bytes = reinterpret_cast<const unsigned char*>(&addr.sin6_addr);
      CHECK(addr.sin6_family == AF_INET6 && bytes[0] == 0x20 && bytes[1] == 0x01 &&
            bytes[2] == 0x0d && bytes[3] == 0xb8 && bytes[15] == 0x42);
      break;
    }
    case MDNS_RECORDTYPE_TXT: {
      ++seen.txt;
      CHECK(owner == "web._http._tcp.local." && name_length == 2);
      CHECK(rclass == (MDNS_CACHE_FLUSH | MDNS_CLASS_IN) && ttl == 4500);
      mdns_record_txt_t pairs[4];
      const size_t count =
          mdns_record_parse_txt(data, size, record_offset, record_length, pairs, 4);
      CHECK(count == 2);
      if (count != 2) break;
      CHECK(str(pairs[0].key) == "path" && str(pairs[0].value) == "/docs");
      CHECK(str(pairs[1].key) == "secure" && pairs[1].value.length == 0);
      break;
    }
    default:
      CHECK(false);
  }
  return 0;
}

}  // namespace

int main() {
  mdns_record_t ptr = {};
  ptr.name = text("_http._tcp.local.");
  ptr.type = MDNS_RECORDTYPE_PTR;
  ptr.data.ptr.name = text("web._http._tcp.local.");
  ptr.rclass = MDNS_CLASS_IN;
  ptr.ttl = 4500;

  mdns_record_t srv = {};
  srv.name = text("web._http._tcp.local.");
  srv.type = MDNS_RECORDTYPE_SRV;
  srv.data.srv.priority = 1;
  srv.data.srv.weight = 5;
  srv.data.srv.port = 8080;
  srv.data.srv.name = text("host.local.");
  srv.rclass = MDNS_CACHE_FLUSH | MDNS_CLASS_IN;
  srv.ttl = 120;

  mdns_record_t a = srv;
  a.name = text("host.local.");
  a.type = MDNS_RECORDTYPE_A;
  a.data.a.addr = sockaddr_in();
  a.data.a.addr.sin_family = AF_INET;
  a.data.a.addr.sin_addr.s_addr = htonl(0xC0A8010A);  // 192.168.1.10

  mdns_record_t aaaa = a;
  aaaa.type = MDNS_RECORDTYPE_AAAA;
  aaaa.data.aaaa.addr = sockaddr_in6();
  aaaa.data.aaaa.addr.sin6_family = AF_INET6;
  const unsigned char ipv6[16] = {0x20, 0x01, 0x0d, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x42};
  std::memcpy(&aaaa.data.aaaa.addr.sin6_addr, ipv6, 16);  // 2001:db8::42

  mdns_record_t txt[2] = {};
  for (mdns_record_t& t : txt) {
    t.name = text("web._http._tcp.local.");
    t.type = MDNS_RECORDTYPE_TXT;
  }
  txt[0].data.txt.key = text("path");
  txt[0].data.txt.value = text("/docs");
  txt[1].data.txt.key = text("secure");
  txt[1].data.txt.value = text("");

  // The 12-byte header: query 0x1234, a response, one answer and four
  // additional records (the TXT entries make one).
  std::uint32_t storage[128];  // the library wants 32-bit aligned buffers
  unsigned char* buffer = reinterpret_cast<unsigned char*>(storage);
  const unsigned char header[12] = {0x12, 0x34, 0x84, 0, 0, 0, 0, 1, 0, 0, 0, 4};
  std::memcpy(buffer, header, sizeof header);
  const size_t capacity = sizeof storage;
  mdns_string_table_t table = {};
  void* end = buffer + sizeof header;
  end = mdns_answer_add_record(buffer, capacity, end, ptr, &table);
  end = mdns_answer_add_record(buffer, capacity, end, srv, &table);
  end = mdns_answer_add_record(buffer, capacity, end, a, &table);
  end = mdns_answer_add_record(buffer, capacity, end, aaaa, &table);
  end = mdns_answer_add_txt_record(buffer, capacity, end, txt, 2, MDNS_CACHE_FLUSH | MDNS_CLASS_IN,
                                   4500, &table);
  CHECK(end != nullptr);
  if (end == nullptr) return 1;
  const size_t size = MDNS_POINTER_DIFF(end, buffer);
  // Header 12; PTR 18 + 10 + 6; SRV 2 + 10 + 13; A 2 + 10 + 4; AAAA 2 + 10 + 16;
  // TXT 2 + 10 + 11 ("path=/docs") + 8 ("secure=").
  CHECK(size == 12 + 34 + 25 + 16 + 28 + 31);

  Seen seen;
  size_t offset = sizeof header;
  CHECK(mdns_records_parse(0, nullptr, 0, buffer, size, &offset, MDNS_ENTRYTYPE_ANSWER, 0x1234, 1,
                           on_record, &seen) == 1);
  CHECK(mdns_records_parse(0, nullptr, 0, buffer, size, &offset, MDNS_ENTRYTYPE_ADDITIONAL, 0x1234,
                           4, on_record, &seen) == 4);
  CHECK(offset == size);
  CHECK(seen.ptr == 1 && seen.srv == 1 && seen.a == 1 && seen.aaaa == 1 && seen.txt == 1);

  // A buffer too small for the next record makes the writer return null.
  mdns_string_table_t fresh = {};
  CHECK(mdns_answer_add_record(buffer, 20, buffer + sizeof header, srv, &fresh) == nullptr);

  std::printf("mdns (C++): %s\n", failures == 0 ? "ok" : "FAILED");
  return failures == 0 ? 0 : 1;
}
