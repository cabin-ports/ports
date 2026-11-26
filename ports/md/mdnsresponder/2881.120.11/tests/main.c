/* Exercises the parts of the DNS-SD client library that need no daemon:
   TXT record building and parsing, service name escaping, and the error a
   request returns when no daemon answers. */
#ifdef __linux__
#define _POSIX_C_SOURCE 200112L /* setenv */
#endif

#include <dns_sd.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

static void check(int ok, int line, const char *what)
{
  if (!ok) {
    fprintf(stderr, "main.c:%d: check failed: %s\n", line, what);
    ++failures;
  }
}

#define CHECK(expr) check((expr) != 0, __LINE__, #expr)

static void txt_record(void)
{
  static const char built[] = "\012path=/home" "\006secure";
  static const char replaced[] = "\006secure" "\006path=/";
  TXTRecordRef txt;
  const void *bytes;
  const void *value;
  uint16_t length;
  uint8_t value_len = 0;
  char key[16];

  TXTRecordCreate(&txt, 0, NULL);
  CHECK(TXTRecordGetLength(&txt) == 0);
  CHECK(TXTRecordSetValue(&txt, "path", 5, "/home") == kDNSServiceErr_NoError);
  CHECK(TXTRecordSetValue(&txt, "secure", 0, NULL) == kDNSServiceErr_NoError);
  CHECK(TXTRecordSetValue(&txt, "a=b", 1, "x") == kDNSServiceErr_Invalid);

  length = TXTRecordGetLength(&txt);
  bytes = TXTRecordGetBytesPtr(&txt);
  CHECK(length == sizeof built - 1);
  CHECK(length == sizeof built - 1 && memcmp(bytes, built, length) == 0);
  CHECK(TXTRecordGetCount(length, bytes) == 2);
  CHECK(TXTRecordContainsKey(length, bytes, "PATH") == 1);
  CHECK(TXTRecordContainsKey(length, bytes, "secure") == 1);
  CHECK(TXTRecordContainsKey(length, bytes, "missing") == 0);

  value = TXTRecordGetValuePtr(length, bytes, "path", &value_len);
  CHECK(value != NULL && value_len == 5 && memcmp(value, "/home", 5) == 0);
  CHECK(TXTRecordGetValuePtr(length, bytes, "secure", &value_len) == NULL);

  value = bytes;
  value_len = 99;
  CHECK(TXTRecordGetItemAtIndex(length, bytes, 1, sizeof key, key, &value_len, &value) ==
        kDNSServiceErr_NoError);
  CHECK(strcmp(key, "secure") == 0 && value == NULL && value_len == 0);
  CHECK(TXTRecordGetItemAtIndex(length, bytes, 2, sizeof key, key, &value_len, &value) ==
        kDNSServiceErr_Invalid);

  /* Setting an existing key replaces it at the end of the record. */
  CHECK(TXTRecordSetValue(&txt, "path", 1, "/") == kDNSServiceErr_NoError);
  length = TXTRecordGetLength(&txt);
  CHECK(length == sizeof replaced - 1 &&
        memcmp(TXTRecordGetBytesPtr(&txt), replaced, length) == 0);

  CHECK(TXTRecordRemoveValue(&txt, "secure") == kDNSServiceErr_NoError);
  CHECK(TXTRecordRemoveValue(&txt, "secure") == kDNSServiceErr_NoSuchKey);
  length = TXTRecordGetLength(&txt);
  CHECK(length == 7 && memcmp(TXTRecordGetBytesPtr(&txt), "\006path=/", 7) == 0);
  TXTRecordDeallocate(&txt);
}

static void full_name(void)
{
  char full[kDNSServiceMaxDomainName];

  CHECK(DNSServiceConstructFullName(full, "My Printer.2\\B", "_ipp._tcp", "local") ==
        kDNSServiceErr_NoError);
  CHECK(strcmp(full, "My\\032Printer\\.2\\\\B._ipp._tcp.local.") == 0);
  CHECK(DNSServiceConstructFullName(full, NULL, "_http._tcp.", "local.") == kDNSServiceErr_NoError);
  CHECK(strcmp(full, "_http._tcp.local.") == 0);
  CHECK(DNSServiceConstructFullName(full, "x", "_http._sctp", "local.") == kDNSServiceErr_BadParam);
  CHECK(DNSServiceConstructFullName(full, "x", "_tcp", "local.") == kDNSServiceErr_BadParam);
}

static void DNSSD_API on_browse(DNSServiceRef ref, DNSServiceFlags flags, uint32_t interface_index,
                                DNSServiceErrorType error, const char *name, const char *type,
                                const char *domain, void *context)
{
  (void)ref;
  (void)flags;
  (void)interface_index;
  (void)error;
  (void)name;
  (void)type;
  (void)domain;
  ++*(int *)context;
}

static void no_daemon(void)
{
  DNSServiceRef ref = NULL;
  int replies = 0;

  /* Point the client at a daemon address where nothing listens. */
#ifdef _WIN32
  CHECK(SetEnvironmentVariableA("MDNS_TCP_SERVERPORT_CENTENNIAL", "1"));
#else
  CHECK(setenv("DNSSD_UDS_PATH", "/nonexistent-cabin-ports-mdnsresponder/socket", 1) == 0);
#endif
  CHECK(DNSServiceBrowse(&ref, 0, 0, NULL, NULL, on_browse, &replies) == kDNSServiceErr_BadParam);
  CHECK(DNSServiceBrowse(&ref, 0, 0, "_http._tcp", NULL, on_browse, &replies) ==
        kDNSServiceErr_ServiceNotRunning);
  CHECK(ref == NULL && replies == 0);
}

int main(void)
{
  txt_record();
  full_name();
  no_daemon();
  if (failures != 0) {
    fprintf(stderr, "%d check(s) failed\n", failures);
    return EXIT_FAILURE;
  }
  puts("mdnsresponder C consumer: all checks passed");
  return EXIT_SUCCESS;
}
