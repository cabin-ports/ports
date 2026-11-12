/* Builds a dictionary holding every scalar type, round-trips it through
   the XML and binary formats and a JSON-compatible array through JSON,
   checks every value, the XML date and unsigned integer text, format
   detection and the version, and checks that malformed and truncated input
   is rejected. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <plist/plist.h>

static const char blob[] = {0x00, 0x01, 0x7f, (char)0xff};
static const int64_t when = 1700000000; /* 2023-11-14T22:13:20Z */

static int fail(const char *what) {
  fprintf(stderr, "%s\n", what);
  return 1;
}

static plist_t build(void) {
  plist_t dict = plist_new_dict();
  plist_t items = plist_new_array();
  plist_array_append_item(items, plist_new_int(1));
  plist_array_append_item(items, plist_new_string("two"));
  plist_dict_set_item(dict, "name", plist_new_string("Cabin"));
  plist_dict_set_item(dict, "count", plist_new_int(-42));
  plist_dict_set_item(dict, "big", plist_new_uint(UINT64_C(9223372036854775813)));
  plist_dict_set_item(dict, "ratio", plist_new_real(0.25));
  plist_dict_set_item(dict, "ok", plist_new_bool(1));
  plist_dict_set_item(dict, "blob", plist_new_data(blob, sizeof blob));
  plist_dict_set_item(dict, "when", plist_new_unix_date(when));
  plist_dict_set_item(dict, "items", items);
  return dict;
}

static int check(plist_t dict, const char *format) {
  const char *s;
  uint64_t len = 0, u = 0;
  int64_t i = 0;
  double d = 0;
  uint8_t b = 0;
  plist_t items = plist_dict_get_item(dict, "items");

  s = plist_get_string_ptr(plist_dict_get_item(dict, "name"), &len);
  if (s == NULL || len != 5 || strcmp(s, "Cabin") != 0) return fail(format);
  plist_get_int_val(plist_dict_get_item(dict, "count"), &i);
  if (i != -42) return fail(format);
  plist_get_uint_val(plist_dict_get_item(dict, "big"), &u);
  if (u != UINT64_C(9223372036854775813)) return fail(format);
  plist_get_real_val(plist_dict_get_item(dict, "ratio"), &d);
  if (d != 0.25) return fail(format);
  plist_get_bool_val(plist_dict_get_item(dict, "ok"), &b);
  if (b != 1) return fail(format);
  s = plist_get_data_ptr(plist_dict_get_item(dict, "blob"), &len);
  if (s == NULL || len != sizeof blob || memcmp(s, blob, sizeof blob) != 0) {
    return fail(format);
  }
  i = 0;
  plist_get_unix_date_val(plist_dict_get_item(dict, "when"), &i);
  if (i != when) return fail(format);
  if (plist_get_node_type(items) != PLIST_ARRAY ||
      plist_array_get_size(items) != 2) {
    return fail(format);
  }
  s = plist_get_string_ptr(plist_array_get_item(items, 1), NULL);
  if (s == NULL || strcmp(s, "two") != 0) return fail(format);
  return 0;
}

int main(void) {
  plist_t dict = build();
  plist_t back = NULL;
  plist_format_t format = PLIST_FORMAT_NONE;
  char *out = NULL;
  uint32_t out_len = 0;

  if (check(dict, "built dictionary")) return 1;

  if (plist_to_xml(dict, &out, &out_len) != PLIST_ERR_SUCCESS) {
    return fail("plist_to_xml failed");
  }
  if (strstr(out, "<date>2023-11-14T22:13:20Z</date>") == NULL) {
    return fail("XML date text");
  }
  /* The getters return the same 64 bits either way; the text shows the
     value stayed unsigned. */
  if (strstr(out, "<integer>9223372036854775813</integer>") == NULL) {
    return fail("XML unsigned integer text");
  }
  if (plist_from_memory(out, out_len, &back, &format) != PLIST_ERR_SUCCESS ||
      format != PLIST_FORMAT_XML || check(back, "XML round trip")) {
    return 1;
  }
  plist_free(back);
  back = NULL;
  plist_mem_free(out);
  out = NULL;

  if (plist_to_bin(dict, &out, &out_len) != PLIST_ERR_SUCCESS) {
    return fail("plist_to_bin failed");
  }
  if (!plist_is_binary(out, out_len) ||
      plist_from_memory(out, out_len, &back, &format) != PLIST_ERR_SUCCESS ||
      format != PLIST_FORMAT_BINARY || check(back, "binary round trip")) {
    return 1;
  }
  plist_free(back);
  back = NULL;
  if (plist_from_bin(out, out_len / 2, &back) == PLIST_ERR_SUCCESS) {
    return fail("truncated binary plist parsed");
  }
  plist_mem_free(out);
  out = NULL;

  /* JSON has no date or data type, so only the array goes through it. */
  if (plist_to_json(plist_dict_get_item(dict, "items"), &out, &out_len, 0) !=
      PLIST_ERR_SUCCESS) {
    return fail("plist_to_json failed");
  }
  if (strcmp(out, "[1,\"two\"]") != 0) return fail("JSON text");
  if (plist_from_json(out, out_len, &back) != PLIST_ERR_SUCCESS ||
      plist_array_get_size(back) != 2 ||
      !plist_compare_node_value(
          plist_array_get_item(back, 0),
          plist_array_get_item(plist_dict_get_item(dict, "items"), 0))) {
    return fail("JSON round trip");
  }
  plist_free(back);
  back = NULL;
  plist_mem_free(out);

  {
    static const char bad[] =
        "<plist version=\"1.0\"><dict><key>a</key></plist>";
    if (plist_from_xml(bad, sizeof bad - 1, &back) == PLIST_ERR_SUCCESS) {
      return fail("malformed XML parsed");
    }
  }

  plist_free(dict);
  if (strcmp(libplist_version(), "2.8.1") != 0) return fail("libplist_version");
  printf("libplist %s: XML, binary and JSON round trips ok\n",
         libplist_version());
  return 0;
}
