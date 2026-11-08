#include <cbor.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !CBOR_PRETTY_PRINTER
#error "configuration.h must enable the pretty printer, as CMake does"
#endif

static int failures = 0;

static void check(int ok, const char *what) {
  if (!ok) {
    fprintf(stderr, "FAIL: %s\n", what);
    failures++;
  }
}

/* Serializes item and compares the bytes with the expected encoding. */
static int encodes_as(const cbor_item_t *item, const char *expected,
                      size_t expected_len) {
  unsigned char *buffer = NULL;
  size_t buffer_size = 0;
  size_t length = cbor_serialize_alloc(item, &buffer, &buffer_size);
  int ok = length == expected_len && memcmp(buffer, expected, length) == 0;
  free(buffer);
  return ok;
}

static cbor_item_t *load(const char *data, size_t len,
                         struct cbor_load_result *result) {
  return cbor_load((cbor_data)data, len, result);
}

struct events {
  int arrays;
  uint64_t array_items;
  unsigned uint_sum;
  int maps;
  int keys_k;
  float half;
};

static void on_array_start(void *context, uint64_t size) {
  struct events *events = context;
  events->arrays++;
  events->array_items += size;
}

static void on_uint8(void *context, uint8_t value) {
  ((struct events *)context)->uint_sum += value;
}

static void on_map_start(void *context, uint64_t size) {
  (void)size;
  ((struct events *)context)->maps++;
}

static void on_string(void *context, cbor_data data, uint64_t length) {
  if (length == 1 && data[0] == 'k') {
    ((struct events *)context)->keys_k++;
  }
}

static void on_float2(void *context, float value) {
  ((struct events *)context)->half = value;
}

int main(void) {
  check(strcmp(CBOR_VERSION, "0.14.0") == 0 && cbor_major_version == 0 &&
            cbor_minor_version == 14 && cbor_patch_version == 0,
        "version");

  /* Maps, arrays, strings: RFC 8949 Appendix A's {"a": 1, "b": [2, 3]}. */
  static const char map_bytes[] = "\xa2\x61\x61\x01\x61\x62\x82\x02\x03";
  cbor_item_t *array = cbor_new_definite_array(2);
  check(cbor_array_push(array, cbor_move(cbor_build_uint8(2))) &&
            cbor_array_push(array, cbor_move(cbor_build_uint8(3))),
        "array push");
  cbor_item_t *map = cbor_new_definite_map(2);
  check(cbor_map_add(map, (struct cbor_pair){
                              .key = cbor_move(cbor_build_string("a")),
                              .value = cbor_move(cbor_build_uint8(1))}) &&
            cbor_map_add(map, (struct cbor_pair){
                                  .key = cbor_move(cbor_build_string("b")),
                                  .value = cbor_move(array)}),
        "map add");
  check(encodes_as(map, map_bytes, sizeof map_bytes - 1), "map encoding");

  struct cbor_load_result result;
  cbor_item_t *decoded = load(map_bytes, sizeof map_bytes - 1, &result);
  check(decoded != NULL && result.error.code == CBOR_ERR_NONE &&
            result.read == sizeof map_bytes - 1,
        "map decoding");
  check(decoded != NULL && cbor_isa_map(decoded) &&
            cbor_map_size(decoded) == 2 &&
            cbor_structurally_equal(decoded, map),
        "decoded map equals the built one");
  if (decoded != NULL) {
    cbor_item_t *key = cbor_build_string("b");
    cbor_item_t *value = cbor_map_get(decoded, key, cbor_structurally_equal);
    check(value != NULL && cbor_isa_array(value) &&
            cbor_array_size(value) == 2 &&
            cbor_get_int(cbor_array_handle(value)[1]) == 3,
          "map lookup");
    if (value != NULL) {
      cbor_decref(&value);
    }
    cbor_decref(&key);
    cbor_decref(&decoded);
  }
  cbor_decref(&map);

  /* Text strings: the bundled UTF-8 decoder counts code points. */
  static const char water[] = "\x63\xe6\xb0\xb4";
  cbor_item_t *text = load(water, sizeof water - 1, &result);
  check(text != NULL && cbor_isa_string(text) &&
            cbor_string_length(text) == 3 &&
            cbor_string_codepoint_count(text) == 1 &&
            memcmp(cbor_string_handle(text), "\xe6\xb0\xb4", 3) == 0,
        "UTF-8 string");
  if (text != NULL) {
    cbor_decref(&text);
  }
  text = load("\x62\xc3\x28", 3, &result);
  check(text != NULL && cbor_string_codepoint_count(text) == 0,
        "invalid UTF-8 has no code point count");
  if (text != NULL) {
    cbor_decref(&text);
  }

  /* Floats, RFC 8949 Appendix A. */
  cbor_item_t *f = cbor_build_float2(1.5f);
  check(encodes_as(f, "\xf9\x3e\x00", 3), "half encoding");
  cbor_decref(&f);
  f = cbor_build_float4(100000.0f);
  check(encodes_as(f, "\xfa\x47\xc3\x50\x00", 5), "single encoding");
  cbor_decref(&f);
  f = cbor_build_float8(-4.1);
  check(encodes_as(f, "\xfb\xc0\x10\x66\x66\x66\x66\x66\x66", 9),
        "double encoding");
  cbor_decref(&f);
  unsigned char half[3];
  check(cbor_encode_half(65504.0f, half, sizeof half) == 3 &&
            memcmp(half, "\xf9\x7b\xff", 3) == 0,
        "largest half");
  /* Decoded through RFC 8949's half-precision decoder. */
  f = load("\xf9\x00\x01", 3, &result);
  check(f != NULL && cbor_is_float(f) &&
            cbor_float_get_float(f) == ldexp(1.0, -24),
        "smallest half subnormal");
  if (f != NULL) {
    cbor_decref(&f);
  }
  f = load("\xf9\x7c\x00", 3, &result);
  check(f != NULL && isinf(cbor_float_get_float(f)), "half infinity");
  if (f != NULL) {
    cbor_decref(&f);
  }
  f = load("\xfb\x3f\xf1\x99\x99\x99\x99\x99\x9a", 9, &result);
  /* The cast drops the excess precision x87 gives the constant. */
  check(f != NULL && cbor_float_get_float(f) == (double)1.1,
        "double decoding");
  if (f != NULL) {
    cbor_decref(&f);
  }

  /* Tags: 1(1363896240), an epoch-based date and time. */
  static const char tag_bytes[] = "\xc1\x1a\x51\x4b\x67\xb0";
  cbor_item_t *tag = cbor_build_tag(1, cbor_move(cbor_build_uint32(1363896240)));
  check(encodes_as(tag, tag_bytes, sizeof tag_bytes - 1), "tag encoding");
  cbor_decref(&tag);
  tag = load(tag_bytes, sizeof tag_bytes - 1, &result);
  check(tag != NULL && cbor_isa_tag(tag) && cbor_tag_value(tag) == 1,
        "tag decoding");
  if (tag != NULL) {
    cbor_item_t *tagged = cbor_tag_item(tag);
    check(cbor_get_int(tagged) == 1363896240, "tagged item");
    cbor_decref(&tagged);
    cbor_decref(&tag);
  }

  /* Streaming decode of [1, [2, 3], {"k": 1.0}], one callback a call. */
  static const char stream[] =
      "\x83\x01\x82\x02\x03\xa1\x61\x6b\xf9\x3c\x00";
  struct cbor_callbacks callbacks = cbor_empty_callbacks;
  callbacks.array_start = on_array_start;
  callbacks.uint8 = on_uint8;
  callbacks.map_start = on_map_start;
  callbacks.string = on_string;
  callbacks.float2 = on_float2;
  struct events events = {0};
  size_t offset = 0;
  int calls = 0;
  while (offset < sizeof stream - 1) {
    struct cbor_decoder_result r =
        cbor_stream_decode((cbor_data)stream + offset,
                           sizeof stream - 1 - offset, &callbacks, &events);
    if (r.status != CBOR_DECODER_FINISHED) {
      break;
    }
    offset += r.read;
    calls++;
  }
  check(offset == sizeof stream - 1 && calls == 8, "stream consumed");
  check(events.arrays == 2 && events.array_items == 5 &&
            events.uint_sum == 6 && events.maps == 1 && events.keys_k == 1 &&
            events.half == 1.0f,
        "stream callbacks");
  struct cbor_decoder_result partial =
      cbor_stream_decode((cbor_data) "\x19\x01", 2, &callbacks, &events);
  check(partial.status == CBOR_DECODER_NEDATA && partial.required == 3,
        "stream asks for the rest of a uint16");

  /* Truncated and malformed input. */
  check(load("\x82\x01", 2, &result) == NULL &&
            result.error.code == CBOR_ERR_NOTENOUGHDATA,
        "truncated array");
  check(load("\x1c", 1, &result) == NULL &&
            result.error.code == CBOR_ERR_MALFORMATED,
        "reserved additional information");
  check(load("\xff", 1, &result) == NULL &&
            result.error.code == CBOR_ERR_SYNTAXERROR,
        "stray break");

  /* CBOR_MAX_STACK_SIZE: 2048 nested tags decode, 2049 do not. */
  unsigned char *deep = malloc(2049 + 2);
  check(deep != NULL, "malloc");
  if (deep != NULL) {
    memset(deep, 0xc2, 2049);
    deep[2049] = 0x41;
    deep[2050] = 0x01;
    cbor_item_t *nested = cbor_load(deep + 1, 2050, &result);
    check(nested != NULL && result.error.code == CBOR_ERR_NONE,
          "2048 nested tags");
    if (nested != NULL) {
      cbor_decref(&nested);
    }
    check(cbor_load(deep, 2051, &result) == NULL &&
              result.error.code == CBOR_ERR_MEMERROR,
          "2049 nested tags exceed the stack");
    free(deep);
  }

  if (failures != 0) {
    return 1;
  }
  printf("libcbor %s: {\"a\": 1, \"b\": [2, 3]} round-trips\n", CBOR_VERSION);
  return 0;
}
