#include <stdio.h>
#include <string.h>

#include "cn-cbor/cn-cbor.h"

static int failures;

#define CHECK(cond)                                              \
  do {                                                           \
    if (!(cond)) {                                               \
      fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #cond); \
      failures++;                                                \
    }                                                            \
  } while (0)

static cn_cbor *decode(const uint8_t *buf, size_t len) {
  cn_cbor_errback err;
  cn_cbor *cb = cn_cbor_decode(buf, len, &err);
  CHECK(cb != NULL);
  return cb;
}

int main(void) {
  /* RFC 8949 Appendix A examples. */
  static const uint8_t zero[] = {0x00};
  static const uint8_t thousand[] = {0x19, 0x03, 0xe8};
  static const uint8_t minus_one[] = {0x20};
  static const uint8_t ietf[] = {0x64, 0x49, 0x45, 0x54, 0x46};
  static const uint8_t nested[] = {0x83, 0x01, 0x82, 0x02, 0x03, 0x82, 0x04, 0x05};
  static const uint8_t map_bytes[] = {0xa2, 0x61, 0x61, 0x01, 0x61, 0x62, 0x82, 0x02, 0x03};
  cn_cbor *cb, *sub;

  cb = decode(zero, sizeof zero);
  if (cb) {
    CHECK(cb->type == CN_CBOR_UINT && cb->v.uint == 0);
    cn_cbor_free(cb);
  }

  cb = decode(thousand, sizeof thousand);
  if (cb) {
    CHECK(cb->type == CN_CBOR_UINT && cb->v.uint == 1000);
    cn_cbor_free(cb);
  }

  cb = decode(minus_one, sizeof minus_one);
  if (cb) {
    CHECK(cb->type == CN_CBOR_INT && cb->v.sint == -1);
    cn_cbor_free(cb);
  }

  cb = decode(ietf, sizeof ietf);
  if (cb) {
    CHECK(cb->type == CN_CBOR_TEXT && cb->length == 4);
    CHECK(memcmp(cb->v.str, "IETF", 4) == 0);
    cn_cbor_free(cb);
  }

  cb = decode(nested, sizeof nested);
  if (cb) {
    CHECK(cb->type == CN_CBOR_ARRAY && cb->length == 3);
    CHECK(cn_cbor_index(cb, 0) && cn_cbor_index(cb, 0)->v.uint == 1);
    sub = cn_cbor_index(cb, 2);
    CHECK(sub && sub->type == CN_CBOR_ARRAY && sub->length == 2);
    CHECK(sub && cn_cbor_index(sub, 1) && cn_cbor_index(sub, 1)->v.uint == 5);
    CHECK(cn_cbor_index(cb, 3) == NULL);
    cn_cbor_free(cb);
  }

  cb = decode(map_bytes, sizeof map_bytes);
  if (cb) {
    CHECK(cb->type == CN_CBOR_MAP && cb->length == 4);
    CHECK(cn_cbor_mapget_string(cb, "a") && cn_cbor_mapget_string(cb, "a")->v.uint == 1);
    sub = cn_cbor_mapget_string(cb, "b");
    CHECK(sub && sub->type == CN_CBOR_ARRAY && sub->length == 2);
    CHECK(sub && cn_cbor_index(sub, 1) && cn_cbor_index(sub, 1)->v.uint == 3);
    CHECK(cn_cbor_mapget_string(cb, "c") == NULL);
    cn_cbor_free(cb);
  }

  /* Build {"a": 1, "b": [2, 3]} and check the exact encoding. */
  {
    cn_cbor_errback err;
    uint8_t out[16];
    ssize_t n;
    cn_cbor *map = cn_cbor_map_create(&err);
    cn_cbor *arr = cn_cbor_array_create(&err);
    CHECK(map && arr);
    if (map && arr) {
      CHECK(cn_cbor_array_append(arr, cn_cbor_int_create(2, &err), &err));
      CHECK(cn_cbor_array_append(arr, cn_cbor_int_create(3, &err), &err));
      CHECK(cn_cbor_mapput_string(map, "a", cn_cbor_int_create(1, &err), &err));
      CHECK(cn_cbor_mapput_string(map, "b", arr, &err));
      n = cn_cbor_encoder_write(out, 0, sizeof out, map);
      CHECK(n == (ssize_t)sizeof map_bytes);
      CHECK(n == (ssize_t)sizeof map_bytes && memcmp(out, map_bytes, sizeof map_bytes) == 0);
      cn_cbor_free(map);
    }
  }

  /* Malformed input: a truncated uint16, and trailing data after a value. */
  {
    static const uint8_t truncated[] = {0x19, 0x03};
    static const uint8_t trailing[] = {0x00, 0x00};
    cn_cbor_errback err;
    CHECK(cn_cbor_decode(truncated, sizeof truncated, &err) == NULL);
    CHECK(err.err == CN_CBOR_ERR_OUT_OF_DATA);
    CHECK(cn_cbor_decode(trailing, sizeof trailing, &err) == NULL);
    CHECK(err.err == CN_CBOR_ERR_NOT_ALL_DATA_CONSUMED);
  }

  return failures ? 1 : 0;
}
