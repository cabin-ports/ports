/* Consumer test from C89: URIs parsed, resolved, shortened, normalized,
 * edited and written back, queries, escaping, file names and IPv4 in the
 * char API, the wchar_t API, and a custom memory manager. */
#include <uriparser/Uri.h>
#include <uriparser/UriIp4.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

static void check(int ok, const char *what, int line) {
  if (!ok) {
    fprintf(stderr, "main.c:%d: check failed: %s\n", line, what);
    ++failures;
  }
}
#define CHECK(cond) check((cond) != 0, #cond, __LINE__)

static int range_is(const UriTextRangeA *range, const char *text) {
  size_t length = strlen(text);
  if (range->first == NULL) return 0;
  return (size_t)(range->afterLast - range->first) == length &&
         memcmp(range->first, text, length) == 0;
}

static int wide_range_is(const UriTextRangeW *range, const wchar_t *text) {
  const wchar_t *p = range->first;
  if (p == NULL) return 0;
  while (*text != L'\0' && p != range->afterLast && *p == *text) {
    ++p;
    ++text;
  }
  return *text == L'\0' && p == range->afterLast;
}

static int wide_equals(const wchar_t *a, const wchar_t *b) {
  while (*a != L'\0' && *a == *b) {
    ++a;
    ++b;
  }
  return *a == *b;
}

/* uri written back into out, or "" if that fails. */
static const char *text_of(const UriUriA *uri, char *out, int size) {
  int required = 0;
  out[0] = '\0';
  if (uriToStringCharsRequiredA(uri, &required) != URI_SUCCESS || required + 1 > size ||
      uriToStringA(out, uri, size, NULL) != URI_SUCCESS) {
    out[0] = '\0';
  }
  return out;
}

/* reference resolved against base, written into out. */
static const char *resolve(const char *base, const char *reference, char *out, int size) {
  UriUriA b, r, resolved;
  out[0] = '\0';
  if (uriParseSingleUriA(&b, base, NULL) != URI_SUCCESS) return out;
  if (uriParseSingleUriA(&r, reference, NULL) == URI_SUCCESS) {
    if (uriAddBaseUriA(&resolved, &r, &b) == URI_SUCCESS) {
      text_of(&resolved, out, size);
      uriFreeUriMembersA(&resolved);
    }
    uriFreeUriMembersA(&r);
  }
  uriFreeUriMembersA(&b);
  return out;
}

static int allocations = 0;
static int releases = 0;

static void *counting_malloc(UriMemoryManager *memory, size_t size) {
  (void)memory;
  ++allocations;
  return malloc(size);
}

static void counting_free(UriMemoryManager *memory, void *ptr) {
  (void)memory;
  if (ptr != NULL) ++releases;
  free(ptr);
}

int main(void) {
  char buffer[256];
  const char *error_pos = NULL;
  UriUriA uri;

  CHECK(strcmp(uriBaseRuntimeVersionA(), "1.0.3") == 0);
  CHECK(strcmp(URI_VER_ANSI, "1.0.3") == 0);

  /* Every component of an absolute URI. */
  {
    const char *text = "http://user:pw@www.example.com:8080/a/b%20c/d?x=1&y=2#frag";
    CHECK(uriParseSingleUriA(&uri, text, &error_pos) == URI_SUCCESS);
    CHECK(range_is(&uri.scheme, "http"));
    CHECK(range_is(&uri.userInfo, "user:pw"));
    CHECK(range_is(&uri.hostText, "www.example.com"));
    CHECK(uri.hostData.ip4 == NULL && uri.hostData.ip6 == NULL);
    CHECK(range_is(&uri.portText, "8080"));
    CHECK(uri.pathHead != NULL && range_is(&uri.pathHead->text, "a"));
    CHECK(uri.pathHead != NULL && uri.pathHead->next != NULL &&
          range_is(&uri.pathHead->next->text, "b%20c"));
    CHECK(uri.pathTail != NULL && range_is(&uri.pathTail->text, "d"));
    CHECK(range_is(&uri.query, "x=1&y=2"));
    CHECK(range_is(&uri.fragment, "frag"));
    CHECK(strcmp(text_of(&uri, buffer, sizeof buffer), text) == 0);
    uriFreeUriMembersA(&uri);
  }

  /* A syntax error reports where it is. */
  {
    const char *bad = "http://a b/";
    CHECK(uriParseSingleUriA(&uri, bad, &error_pos) == URI_ERROR_SYNTAX);
    CHECK(error_pos == bad + 8);
  }

  /* IPv4 and IPv6 hosts. */
  {
    unsigned char octets[4];
    const char *ip = "192.168.0.255";
    CHECK(uriParseIpFourAddressA(octets, ip, ip + strlen(ip)) == URI_SUCCESS);
    CHECK(octets[0] == 192 && octets[1] == 168 && octets[2] == 0 && octets[3] == 255);
    ip = "256.1.1.1";
    CHECK(uriParseIpFourAddressA(octets, ip, ip + strlen(ip)) != URI_SUCCESS);
    CHECK(uriParseSingleUriA(&uri, "http://10.0.0.1/", NULL) == URI_SUCCESS);
    CHECK(uri.hostData.ip4 != NULL && uri.hostData.ip4->data[0] == 10 &&
          uri.hostData.ip4->data[3] == 1);
    uriFreeUriMembersA(&uri);
    CHECK(uriParseSingleUriA(&uri, "http://[::1]:80/", NULL) == URI_SUCCESS);
    CHECK(uri.hostData.ip6 != NULL && uri.hostData.ip6->data[15] == 1 &&
          uri.hostData.ip6->data[0] == 0);
    CHECK(range_is(&uri.hostText, "::1"));
    uriFreeUriMembersA(&uri);
  }

  /* Reference resolution, from RFC 3986 section 5.4. */
  {
    const char *base = "http://a/b/c/d;p?q";
    CHECK(strcmp(resolve(base, "g", buffer, sizeof buffer), "http://a/b/c/g") == 0);
    CHECK(strcmp(resolve(base, "../g", buffer, sizeof buffer), "http://a/b/g") == 0);
    CHECK(strcmp(resolve(base, "../../../g", buffer, sizeof buffer), "http://a/g") == 0);
    CHECK(strcmp(resolve(base, "?y", buffer, sizeof buffer), "http://a/b/c/d;p?y") == 0);
    CHECK(strcmp(resolve(base, "#s", buffer, sizeof buffer), "http://a/b/c/d;p?q#s") == 0);
    CHECK(strcmp(resolve(base, "g/./h/../i", buffer, sizeof buffer), "http://a/b/c/g/i") == 0);
  }

  /* The reverse: a reference relative to a base, and equality. */
  {
    UriUriA absolute, base, relative, again;
    CHECK(uriParseSingleUriA(&absolute, "http://a/b/c/x/y", NULL) == URI_SUCCESS);
    CHECK(uriParseSingleUriA(&base, "http://a/b/c/d", NULL) == URI_SUCCESS);
    CHECK(uriRemoveBaseUriA(&relative, &absolute, &base, URI_FALSE) == URI_SUCCESS);
    CHECK(strcmp(text_of(&relative, buffer, sizeof buffer), "x/y") == 0);
    CHECK(uriParseSingleUriA(&again, "http://a/b/c/x/y", NULL) == URI_SUCCESS);
    CHECK(uriEqualsUriA(&absolute, &again));
    CHECK(!uriEqualsUriA(&absolute, &base));
    uriFreeUriMembersA(&again);
    uriFreeUriMembersA(&relative);
    uriFreeUriMembersA(&base);
    uriFreeUriMembersA(&absolute);
  }

  /* Syntax-based normalization: case and dot segments. */
  {
    CHECK(uriParseSingleUriA(&uri, "HTTP://User@Example.COM:8080/a/./b/../c?Q#F", NULL) ==
          URI_SUCCESS);
    CHECK(uriNormalizeSyntaxMaskRequiredA(&uri) != URI_NORMALIZED);
    CHECK(uriNormalizeSyntaxA(&uri) == URI_SUCCESS);
    CHECK(strcmp(text_of(&uri, buffer, sizeof buffer), "http://User@example.com:8080/a/c?Q#F") ==
          0);
    CHECK(uriNormalizeSyntaxMaskRequiredA(&uri) == URI_NORMALIZED);
    uriFreeUriMembersA(&uri);
  }

  /* Editing components in place. */
  {
    const char *scheme = "https", *host = "example.org", *port = "8443";
    const char *path = "/new/path", *query = "k=v", *bad_port = "8x";
    CHECK(uriParseSingleUriA(&uri, "http://example.com/old", NULL) == URI_SUCCESS);
    CHECK(uriSetSchemeA(&uri, scheme, scheme + strlen(scheme)) == URI_SUCCESS);
    CHECK(uriSetHostAutoA(&uri, host, host + strlen(host)) == URI_SUCCESS);
    CHECK(uriSetPortTextA(&uri, port, port + strlen(port)) == URI_SUCCESS);
    CHECK(uriSetPathA(&uri, path, path + strlen(path)) == URI_SUCCESS);
    CHECK(uriSetQueryA(&uri, query, query + strlen(query)) == URI_SUCCESS);
    CHECK(strcmp(text_of(&uri, buffer, sizeof buffer), "https://example.org:8443/new/path?k=v") ==
          0);
    /* A malformed port is rejected and leaves the URI as it was. */
    CHECK(uriSetPortTextA(&uri, bad_port, bad_port + 2) == URI_ERROR_SYNTAX);
    CHECK(strcmp(text_of(&uri, buffer, sizeof buffer), "https://example.org:8443/new/path?k=v") ==
          0);
    uriFreeUriMembersA(&uri);
  }

  /* Writing into a buffer that is too small. */
  {
    char small[8];
    int written = 0;
    CHECK(uriParseSingleUriA(&uri, "http://example.com/", NULL) == URI_SUCCESS);
    CHECK(uriToStringA(small, &uri, sizeof small, &written) == URI_ERROR_TOSTRING_TOO_LONG);
    uriFreeUriMembersA(&uri);
  }

  /* Query strings, split and joined. */
  {
    const char *query = "a=1&b=x%20y&c";
    UriQueryListA *items = NULL;
    int count = 0;
    char *joined = NULL;
    CHECK(uriDissectQueryMallocA(&items, &count, query, query + strlen(query)) == URI_SUCCESS);
    CHECK(count == 3);
    if (count == 3 && items != NULL) {
      CHECK(items->value != NULL && strcmp(items->key, "a") == 0 &&
            strcmp(items->value, "1") == 0);
      CHECK(items->next->value != NULL && strcmp(items->next->key, "b") == 0 &&
            strcmp(items->next->value, "x y") == 0);
      CHECK(strcmp(items->next->next->key, "c") == 0 && items->next->next->value == NULL);
      CHECK(uriComposeQueryMallocA(&joined, items) == URI_SUCCESS);
      CHECK(joined != NULL && strcmp(joined, "a=1&b=x+y&c") == 0);
      free(joined);
    }
    uriFreeQueryListA(items);
  }

  /* Percent-encoding. */
  {
    char escaped[64];
    CHECK(strcmp((uriEscapeA("a b/c&d~", escaped, URI_FALSE, URI_FALSE), escaped),
                 "a%20b%2Fc%26d~") == 0);
    /* Returns the new position of the terminating zero. */
    CHECK(uriUnescapeInPlaceA(escaped) == escaped + 8);
    CHECK(strcmp(escaped, "a b/c&d~") == 0);
  }

  /* File names and file: URIs. */
  {
    char name[64];
    CHECK(uriUnixFilenameToUriStringA("/tmp/a b.txt", buffer) == URI_SUCCESS);
    CHECK(strcmp(buffer, "file:///tmp/a%20b.txt") == 0);
    CHECK(uriUriStringToUnixFilenameA(buffer, name) == URI_SUCCESS);
    CHECK(strcmp(name, "/tmp/a b.txt") == 0);
    CHECK(uriWindowsFilenameToUriStringA("C:\\dir\\a b.txt", buffer) == URI_SUCCESS);
    CHECK(strcmp(buffer, "file:///C:/dir/a%20b.txt") == 0);
  }

  /* The wchar_t API. */
  {
    UriUriW wide;
    const wchar_t *wide_error = NULL;
    CHECK(wide_equals(uriBaseRuntimeVersionW(), L"1.0.3"));
    CHECK(uriParseSingleUriW(&wide, L"https://example.org/p?q=1", &wide_error) ==
          URI_SUCCESS);
    CHECK(wide_range_is(&wide.scheme, L"https"));
    CHECK(wide_range_is(&wide.hostText, L"example.org"));
    CHECK(wide.pathHead != NULL && wide_range_is(&wide.pathHead->text, L"p"));
    CHECK(wide_range_is(&wide.query, L"q=1"));
    uriFreeUriMembersW(&wide);
  }

  /* A caller's memory manager carries every allocation, and
   * uriEmulateReallocarray() rejects a size that overflows size_t. */
  {
    UriMemoryManager backend = {counting_malloc, NULL, NULL, NULL, counting_free, NULL};
    UriMemoryManager incomplete = {NULL, NULL, NULL, NULL, counting_free, NULL};
    UriMemoryManager memory;
    const char *text = "http://example.com/a/./b/../c?x#y";
    size_t half = (size_t)-1 / 2 + 2; /* half * 2 wraps around to 2 */
    void *block;
    int before;

    CHECK(uriCompleteMemoryManager(&memory, &incomplete) ==
          URI_ERROR_MEMORY_MANAGER_INCOMPLETE);
    CHECK(uriCompleteMemoryManager(&memory, &backend) == URI_SUCCESS);
    CHECK(uriTestMemoryManager(&memory) == URI_SUCCESS);
    before = allocations;

    CHECK(uriParseSingleUriExMmA(&uri, text, text + strlen(text), NULL, &memory) ==
          URI_SUCCESS);
    CHECK(uriNormalizeSyntaxExMmA(&uri, URI_NORMALIZE_PATH, &memory) == URI_SUCCESS);
    CHECK(strcmp(text_of(&uri, buffer, sizeof buffer), "http://example.com/a/c?x#y") == 0);
    CHECK(uriFreeUriMembersMmA(&uri, &memory) == URI_SUCCESS);
    CHECK(allocations > before && allocations == releases);

    before = allocations;
    errno = 0;
    block = uriEmulateReallocarray(&memory, NULL, half, 2);
    CHECK(block == NULL && errno == ENOMEM);
    CHECK(allocations == before);
    if (block != NULL) memory.free(&memory, block);
  }

  if (failures != 0) {
    fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  puts("uriparser consumer test passed");
  return 0;
}
