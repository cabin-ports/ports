// The C API from C++, where common.h drops the restrict qualifier.
#include <cbor.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char *what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    failures++;
  }
}

std::vector<unsigned char> serialize(const cbor_item_t *item) {
  std::vector<unsigned char> out(cbor_serialized_size(item));
  out.resize(cbor_serialize(item, out.data(), out.size()));
  return out;
}

}  // namespace

int main() {
  check(std::strcmp(CBOR_VERSION, "0.14.0") == 0, "CBOR_VERSION");

  // {"name": "cabin", "n": -500, "raw": h'00ff'}
  static const unsigned char expected[] = {
      0xa3, 0x64, 'n',  'a',  'm',  'e',  0x65, 'c',  'a',  'b',  'i',
      'n',  0x61, 'n',  0x39, 0x01, 0xf3, 0x63, 'r',  'a',  'w',  0x42,
      0x00, 0xff};
  cbor_item_t *raw = cbor_new_definite_bytestring();
  unsigned char *bytes = static_cast<unsigned char *>(std::malloc(2));
  if (bytes == nullptr) {
    return 1;
  }
  bytes[0] = 0x00;
  bytes[1] = 0xff;
  cbor_bytestring_set_handle(raw, bytes, 2);

  cbor_item_t *map = cbor_new_definite_map(3);
  check(cbor_map_add(map, cbor_pair{cbor_move(cbor_build_string("name")),
                                    cbor_move(cbor_build_string("cabin"))}) &&
            cbor_map_add(map, cbor_pair{cbor_move(cbor_build_string("n")),
                                        cbor_move(cbor_build_negint16(499))}) &&
            cbor_map_add(map, cbor_pair{cbor_move(cbor_build_string("raw")),
                                        cbor_move(raw)}),
        "map add");
  std::vector<unsigned char> encoded = serialize(map);
  check(encoded.size() == sizeof expected &&
            std::memcmp(encoded.data(), expected, sizeof expected) == 0,
        "map encoding");

  cbor_load_result result;
  cbor_item_t *decoded = cbor_load(encoded.data(), encoded.size(), &result);
  check(decoded != nullptr && result.error.code == CBOR_ERR_NONE,
        "map decoding");
  if (decoded != nullptr) {
    cbor_item_t *copy = cbor_copy(decoded);
    check(cbor_structurally_equal(copy, map), "copy equals the original");
    cbor_decref(&copy);

    cbor_item_t *key = cbor_build_string("n");
    cbor_item_t *n = cbor_map_get(decoded, key, cbor_structurally_equal);
    // A negative integer stores -1 - value.
    check(n != nullptr && cbor_isa_negint(n) && cbor_get_int(n) == 499,
          "negative integer");
    if (n != nullptr) {
      cbor_decref(&n);
    }
    cbor_decref(&key);
    cbor_decref(&decoded);
  }
  cbor_decref(&map);

  // A map whose value is cut off.
  static const unsigned char truncated[] = {0xa1, 0x61, 'k'};
  check(cbor_load(truncated, sizeof truncated, &result) == nullptr &&
            result.error.code == CBOR_ERR_NOTENOUGHDATA,
        "truncated map");

  if (failures != 0) {
    return 1;
  }
  std::printf("libcbor %s from C++: -500 encodes as 39 01 f3\n", CBOR_VERSION);
  return 0;
}
