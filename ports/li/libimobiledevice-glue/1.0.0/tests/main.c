/* Checks OPACK encoding against fixed bytes and decodes it back, TLV
 * buffers with little-endian integers and split values, the collection and
 * char buffer, the string helpers, threads with a mutex and thread_once,
 * escape-sequence stripping and address formatting.  It opens no socket. */
#include <libimobiledevice-glue/cbuf.h>
#include <libimobiledevice-glue/collection.h>
#include <libimobiledevice-glue/opack.h>
#include <libimobiledevice-glue/socket.h>
#include <libimobiledevice-glue/termcolors.h>
#include <libimobiledevice-glue/thread.h>
#include <libimobiledevice-glue/tlv.h>
#include <libimobiledevice-glue/utils.h>

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

static void check(int ok, const char *what) {
  if (!ok) {
    fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

static int bytes_equal(const unsigned char *a, unsigned int alen,
                       const unsigned char *b, unsigned int blen) {
  return a != NULL && alen == blen && memcmp(a, b, blen) == 0;
}

static uint64_t uint_at(plist_t array, uint32_t index) {
  uint64_t value = 0;
  plist_get_uint_val(plist_array_get_item(array, index), &value);
  return value;
}

static void test_opack(void) {
  /* Upstream's decoder does not step past a 32- or 64-bit integer
   * (opack.c:307, :311), so each one comes last in its container. */
  /* [5, 40, true, false, "hi", <aa bb>, 0x0102030405060708] */
  static const unsigned char array_bytes[] = {
      0xD7, 0x0D, 0x30, 0x28, 0x01, 0x02, 0x42, 'h',  'i',  0x72, 0xAA,
      0xBB, 0x33, 0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01};
  /* {"k": 0x01020304} */
  static const unsigned char dict_bytes[] = {0xE1, 0x41, 'k',  0x32,
                                             0x04, 0x03, 0x02, 0x01};
  plist_t array = plist_new_array();
  plist_t dict = plist_new_dict();
  plist_t decoded = NULL;
  unsigned char *buf = NULL;
  unsigned int len = 0;
  uint64_t slen = 0;
  const char *s = NULL;
  char text[301];

  plist_array_append_item(array, plist_new_uint(5));
  plist_array_append_item(array, plist_new_uint(40));
  plist_array_append_item(array, plist_new_bool(1));
  plist_array_append_item(array, plist_new_bool(0));
  plist_array_append_item(array, plist_new_string("hi"));
  plist_array_append_item(array, plist_new_data("\xAA\xBB", 2));
  plist_array_append_item(array, plist_new_uint(0x0102030405060708ULL));
  opack_encode_from_plist(array, &buf, &len);
  check(bytes_equal(buf, len, array_bytes, sizeof array_bytes),
        "OPACK array bytes");
  check(opack_decode_to_plist(buf, len, &decoded) == 0 &&
            plist_get_node_type(decoded) == PLIST_ARRAY &&
            plist_array_get_size(decoded) == 7,
        "OPACK array decodes");
  if (plist_array_get_size(decoded) == 7) {
    check(uint_at(decoded, 0) == 5 && uint_at(decoded, 1) == 40,
          "small integers");
    check(plist_bool_val_is_true(plist_array_get_item(decoded, 2)) &&
              !plist_bool_val_is_true(plist_array_get_item(decoded, 3)),
          "booleans");
    s = plist_get_string_ptr(plist_array_get_item(decoded, 4), &slen);
    check(s != NULL && slen == 2 && memcmp(s, "hi", 2) == 0, "string");
    s = plist_get_data_ptr(plist_array_get_item(decoded, 5), &slen);
    check(s != NULL && slen == 2 && memcmp(s, "\xAA\xBB", 2) == 0, "data");
    check(uint_at(decoded, 6) == 0x0102030405060708ULL, "64-bit integer");
  }
  free(buf);
  plist_free(decoded);
  plist_free(array);

  plist_dict_set_item(dict, "k", plist_new_uint(0x01020304));
  buf = NULL;
  decoded = NULL;
  opack_encode_from_plist(dict, &buf, &len);
  check(bytes_equal(buf, len, dict_bytes, sizeof dict_bytes),
        "OPACK dictionary bytes");
  check(opack_decode_to_plist(buf, len, &decoded) == 0 &&
            plist_get_node_type(decoded) == PLIST_DICT,
        "OPACK dictionary decodes");
  if (plist_get_node_type(decoded) == PLIST_DICT) {
    plist_t k = plist_dict_get_item(decoded, "k");
    uint64_t value = 0;
    plist_get_uint_val(k, &value);
    check(k != NULL && value == 0x01020304, "32-bit integer");
  }
  free(buf);
  plist_free(decoded);
  plist_free(dict);

  /* A 300-byte string takes a 0x62 marker and a little-endian length. */
  memset(text, 'x', 300);
  text[300] = '\0';
  array = plist_new_string(text);
  buf = NULL;
  decoded = NULL;
  opack_encode_from_plist(array, &buf, &len);
  check(buf != NULL && len == 303 && buf[0] == 0x62 && buf[1] == 0x2C &&
            buf[2] == 0x01,
        "long string header");
  check(opack_decode_to_plist(buf, len, &decoded) == 0, "long string decodes");
  s = plist_get_string_ptr(decoded, &slen);
  check(s != NULL && slen == 300 && memcmp(s, text, 300) == 0,
        "long string value");
  free(buf);
  plist_free(decoded);
  plist_free(array);
}

static void test_tlv(void) {
  static const unsigned char u16[] = {0x34, 0x12};
  static const unsigned char u64[] = {8, 7, 6, 5, 4, 3, 2, 1};
  static const unsigned char u32[] = {4, 3, 2, 1};
  unsigned char u8 = 0x7F;
  unsigned char big[300];
  tlv_buf_t tlv = tlv_buf_new();
  uint64_t value = 0;
  uint8_t byte = 0;
  void *out = NULL;
  unsigned int out_len = 0;
  int i;

  for (i = 0; i < 300; ++i) {
    big[i] = (unsigned char)i;
  }
  tlv_buf_append(tlv, 1, sizeof u16, (void *)u16);
  tlv_buf_append(tlv, 2, sizeof u64, (void *)u64);
  tlv_buf_append(tlv, 3, sizeof u32, (void *)u32);
  tlv_buf_append(tlv, 4, 1, &u8);
  tlv_buf_append(tlv, 5, sizeof big, big);
  /* The 300 bytes are split into a 255-byte and a 45-byte item. */
  check(tlv->length == 327, "TLV length");
  check(tlv->data[0] == 1 && tlv->data[1] == 2 && tlv->data[2] == 0x34,
        "TLV layout");
  check(tlv->data[23] == 5 && tlv->data[24] == 0xFF && tlv->data[280] == 5 &&
            tlv->data[281] == 45,
        "TLV split items");
  check(tlv_data_get_uint(tlv->data, tlv->length, 1, &value) &&
            value == 0x1234,
        "TLV 16-bit value");
  check(tlv_data_get_uint(tlv->data, tlv->length, 2, &value) &&
            value == 0x0102030405060708ULL,
        "TLV 64-bit value");
  check(tlv_data_get_uint(tlv->data, tlv->length, 3, &value) &&
            value == 0x01020304,
        "TLV 32-bit value");
  check(tlv_data_get_uint8(tlv->data, tlv->length, 4, &byte) && byte == 0x7F,
        "TLV 8-bit value");
  check(!tlv_data_get_uint8(tlv->data, tlv->length, 1, &byte),
        "tlv_data_get_uint8 rejects a 2-byte item");
  check(!tlv_data_get_uint(tlv->data, tlv->length, 9, &value), "missing tag");
  check(tlv_data_copy_data(tlv->data, tlv->length, 5, &out, &out_len) &&
            out_len == 300 && memcmp(out, big, 300) == 0,
        "TLV split items join");
  free(out);
  tlv_buf_free(tlv);
}

static void test_containers(void) {
  struct collection col;
  struct collection copy;
  int a = 1, b = 2, c = 3, sum = 0;
  int *item = NULL;
  unsigned char block[200];
  struct char_buf *cbuf = char_buf_new();

  collection_init(&col);
  collection_add(&col, &a);
  collection_add(&col, &b);
  collection_add(&col, &c);
  check(collection_count(&col) == 3, "collection count");
  check(collection_remove(&col, &b) == 0, "collection_remove");
  FOREACH(item, &col) { sum += *item; }
  ENDFOREACH
  check(sum == 4, "FOREACH skips the removed element");
  collection_copy(&copy, &col);
  check(collection_count(&copy) == 2, "collection_copy");
  collection_free(&copy);
  collection_free(&col);

  memset(block, 'a', sizeof block);
  char_buf_append(cbuf, sizeof block, block);
  char_buf_append(cbuf, 3, (unsigned char *)"xyz");
  char_buf_append(cbuf, sizeof block, block);
  check(cbuf->length == 403 && cbuf->capacity >= 403 &&
            memcmp(cbuf->data + 200, "xyz", 3) == 0 && cbuf->data[402] == 'a',
        "char_buf grows");
  char_buf_free(cbuf);
}

static void test_strings(void) {
  char *s = string_concat("lib", "imobile", "device", (char *)NULL);
  char *uuid = generate_uuid();
  int i;
  int uuid_ok = uuid != NULL && strlen(uuid) == 36;

  check(s != NULL && strcmp(s, "libimobiledevice") == 0, "string_concat");
  s = string_append(s, "-glue", "-1.0", (char *)NULL);
  check(s != NULL && strcmp(s, "libimobiledevice-glue-1.0") == 0,
        "string_append");
  free(s);
  s = string_build_path("var", "db", "lockdown", (char *)NULL);
  check(s != NULL && strcmp(s, "var/db/lockdown") == 0, "string_build_path");
  free(s);
  s = string_toupper("glue-1.0");
  check(strcmp(s, "GLUE-1.0") == 0, "string_toupper");
  free(s);
  s = string_format_size(999);
  check(strcmp(s, "999 Bytes") == 0, "string_format_size bytes");
  free(s);
  s = string_format_size(1500000);
  check(strcmp(s, "1.5 MB") == 0, "string_format_size megabytes");
  free(s);
  for (i = 0; uuid_ok && i < 36; ++i) {
    if (i == 8 || i == 13 || i == 18 || i == 23) {
      uuid_ok = uuid[i] == '-';
    } else {
      uuid_ok = strchr("0123456789ABCDEF", uuid[i]) != NULL;
    }
  }
  check(uuid_ok, "generate_uuid format");
  free(uuid);
}

static mutex_t counter_lock;
static int counter = 0;
static thread_once_t once = THREAD_ONCE_INIT;
static int once_runs = 0;

static void count_once(void) { ++once_runs; }

static void *worker(void *arg) {
  int i;
  (void)arg;
  thread_once(&once, count_once);
  for (i = 0; i < 1000; ++i) {
    mutex_lock(&counter_lock);
    ++counter;
    mutex_unlock(&counter_lock);
  }
  return NULL;
}

static void test_threads(void) {
  THREAD_T threads[4];
  int started = 0, joined = 0, i;

  mutex_init(&counter_lock);
  for (i = 0; i < 4; ++i) {
    if (thread_new(&threads[started], worker, NULL) == 0) {
      ++started;
    }
  }
  for (i = 0; i < started; ++i) {
    if (thread_join(threads[i]) == 0) {
      ++joined;
    }
    thread_free(threads[i]);
  }
  check(started == 4 && joined == 4, "threads start and join");
  check(counter == 4000, "mutex-protected counter");
  check(once_runs == 1, "thread_once runs once");
  mutex_destroy(&counter_lock);
}

static void test_termcolors(void) {
#ifdef _WIN32
  /* MSVC's tmpfile() creates its file in the drive root; tmpnam names one in
   * the user's temporary directory. */
  char name[L_tmpnam];
  FILE *f = tmpnam(name) ? fopen(name, "w+b") : NULL;
#else
  FILE *f = tmpfile();
#endif
  char line[64] = {0};

  check(f != NULL, "temporary file");
  if (f == NULL) {
    return;
  }
  term_colors_init();
  term_colors_set_enabled(0);
  check(cfprintf(f, FG_RED "%s" COLOR_RESET " " STYLE_BRIGHT "%d" COLOR_RESET
                       " type[1]\n",
                 "red", 7) >= 0,
        "cfprintf");
  rewind(f);
  check(fgets(line, sizeof line, f) != NULL &&
            strcmp(line, "red 7 type[1]\n") == 0,
        "escape sequences stripped");
  fclose(f);
#ifdef _WIN32
  remove(name);
#endif
}

static void test_addresses(void) {
  struct sockaddr_in sin;
  struct sockaddr_in6 sin6;
  struct sockaddr other;
  char text[64];

  memset(&sin, 0, sizeof sin);
  sin.sin_family = AF_INET;
  sin.sin_addr.s_addr = htonl(0x7F000001);
  check(socket_addr_to_string((struct sockaddr *)&sin, text, sizeof text) ==
                text &&
            strcmp(text, "127.0.0.1") == 0,
        "IPv4 address");
  memset(&sin6, 0, sizeof sin6);
  sin6.sin6_family = AF_INET6;
  sin6.sin6_addr.s6_addr[15] = 1;
  check(socket_addr_to_string((struct sockaddr *)&sin6, text, sizeof text) ==
                text &&
            strcmp(text, "::1") == 0,
        "IPv6 address");
  memset(&other, 0, sizeof other);
  other.sa_family = AF_UNSPEC;
  check(socket_addr_to_string(&other, text, sizeof text) == NULL,
        "unsupported family");
}

int main(void) {
  test_opack();
  test_tlv();
  test_containers();
  test_strings();
  test_threads();
  test_termcolors();
  test_addresses();
  if (failures == 0) {
    puts("libimobiledevice-glue: ok");
  }
  return failures == 0 ? 0 : 1;
}
