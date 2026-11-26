// Exercises the DNS-SD client library from C++ without a daemon: a TXT
// record built into a caller-supplied buffer, service name escaping, and
// the error a request returns when no daemon answers.
#include <dns_sd.h>

#include <stdlib.h>

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

std::string record_bytes(const TXTRecordRef& txt) {
  return std::string(static_cast<const char*>(TXTRecordGetBytesPtr(&txt)),
                     TXTRecordGetLength(&txt));
}

void txt_record() {
  char buffer[32];
  TXTRecordRef txt;
  TXTRecordCreate(&txt, sizeof buffer, buffer);
  CHECK(TXTRecordSetValue(&txt, "txtvers", 1, "1") == kDNSServiceErr_NoError);
  CHECK(TXTRecordSetValue(&txt, "rp", 7, "printer") == kDNSServiceErr_NoError);
  CHECK(TXTRecordGetBytesPtr(&txt) == buffer);
  CHECK(record_bytes(txt) == std::string("\011txtvers=1\012rp=printer"));

  // The record outgrows the 32-byte buffer and moves to the heap.
  const std::string note(20, 'n');
  CHECK(TXTRecordSetValue(&txt, "note", static_cast<uint8_t>(note.size()), note.data()) ==
        kDNSServiceErr_NoError);
  CHECK(TXTRecordGetBytesPtr(&txt) != buffer);
  CHECK(TXTRecordGetLength(&txt) == 21 + 26);

  const uint16_t length = TXTRecordGetLength(&txt);
  const void* bytes = TXTRecordGetBytesPtr(&txt);
  uint8_t value_len = 0;
  const void* value = TXTRecordGetValuePtr(length, bytes, "RP", &value_len);
  CHECK(value != NULL &&
        std::string(static_cast<const char*>(value), value_len) == "printer");
  CHECK(TXTRecordGetCount(length, bytes) == 3);

  CHECK(TXTRecordRemoveValue(&txt, "rp") == kDNSServiceErr_NoError);
  CHECK(TXTRecordRemoveValue(&txt, "rp") == kDNSServiceErr_NoSuchKey);
  CHECK(record_bytes(txt) == std::string("\011txtvers=1\031note=") + note);
  TXTRecordDeallocate(&txt);
}

void full_name() {
  char full[kDNSServiceMaxDomainName];
  CHECK(DNSServiceConstructFullName(full, "Kitchen\tSpeaker", "_raop._tcp.", "example.com.") ==
        kDNSServiceErr_NoError);
  CHECK(std::string(full) == "Kitchen\\009Speaker._raop._tcp.example.com.");
  CHECK(DNSServiceConstructFullName(full, "x", "_raop._tcp", "") == kDNSServiceErr_BadParam);
}

void DNSSD_API on_browse(DNSServiceRef, DNSServiceFlags, uint32_t, DNSServiceErrorType,
                         const char*, const char*, const char*, void* context) {
  ++*static_cast<int*>(context);
}

void no_daemon() {
  // Point the client at a daemon address where nothing listens.
#ifdef _WIN32
  CHECK(SetEnvironmentVariableA("MDNS_TCP_SERVERPORT_CENTENNIAL", "1") != 0);
#else
  CHECK(setenv("DNSSD_UDS_PATH", "/nonexistent-cabin-ports-mdnsresponder/socket", 1) == 0);
#endif
  DNSServiceRef ref = NULL;
  int replies = 0;
  CHECK(DNSServiceBrowse(&ref, 0, 0, "_raop._tcp", "local.", on_browse, &replies) ==
        kDNSServiceErr_ServiceNotRunning);
  CHECK(ref == NULL && replies == 0);
}

}  // namespace

int main() {
  txt_record();
  full_name();
  no_daemon();
  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return EXIT_FAILURE;
  }
  std::puts("mdnsresponder C++ consumer: all checks passed");
  return EXIT_SUCCESS;
}
