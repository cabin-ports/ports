/* C++ consumer, through the <tinycbor/...> include form: encodes, parses, validates and converts to JSON and
 * diagnostic text, checking exact bytes and values. */
#include <tinycbor/cbor.h>
#include <tinycbor/cborjson.h>

#include <stdio.h>
#include <string.h>

static int failures;

static void check(int ok, const char *what)
{
    if (!ok) {
        fprintf(stderr, "tinycbor C++ test failed: %s\n", what);
        ++failures;
    }
}

/* {"a": 1, "b": [true, null]} */
static const uint8_t doc[] = {0xA2, 0x61, 0x61, 0x01, 0x61, 0x62, 0x82, 0xF5, 0xF6};

static size_t encode_doc(uint8_t *buf, size_t size, int *err)
{
    CborEncoder enc, map, arr;
    cbor_encoder_init(&enc, buf, size, 0);
    *err = cbor_encoder_create_map(&enc, &map, 2);
    *err |= cbor_encode_text_stringz(&map, "a");
    *err |= cbor_encode_int(&map, 1);
    *err |= cbor_encode_text_stringz(&map, "b");
    *err |= cbor_encoder_create_array(&map, &arr, 2);
    *err |= cbor_encode_boolean(&arr, 1);
    *err |= cbor_encode_null(&arr);
    *err |= cbor_encoder_close_container(&map, &arr);
    *err |= cbor_encoder_close_container(&enc, &map);
    /* After an overflow the encoder holds a byte count, not a pointer. */
    return *err == CborNoError ? cbor_encoder_get_buffer_size(&enc, buf) : 0;
}

static void test_encode(void)
{
    uint8_t buf[64], small[4];
    CborEncoder enc;
    int err;
    size_t n = encode_doc(buf, sizeof buf, &err);
    check(err == CborNoError && n == sizeof doc && memcmp(buf, doc, n) == 0, "encoded map bytes");

    err = CborNoError;
    encode_doc(small, sizeof small, &err);
    check(err == CborErrorOutOfMemory, "too small a buffer is reported");

    cbor_encoder_init(&enc, buf, sizeof buf, 0);
    check(cbor_encode_int(&enc, -500) == CborNoError &&
          cbor_encoder_get_buffer_size(&enc, buf) == 3 &&
          buf[0] == 0x39 && buf[1] == 0x01 && buf[2] == 0xF3, "int -500 is 39 01 F3");

    cbor_encoder_init(&enc, buf, sizeof buf, 0);
    check(cbor_encode_float(&enc, 1.5f) == CborNoError &&
          cbor_encoder_get_buffer_size(&enc, buf) == 5 &&
          buf[0] == 0xFA && buf[1] == 0x3F && buf[2] == 0xC0 && buf[3] == 0 && buf[4] == 0,
          "float 1.5 is FA 3F C0 00 00");

    cbor_encoder_init(&enc, buf, sizeof buf, 0);
    check(cbor_encode_float_as_half_float(&enc, 1.5f) == CborNoError &&
          cbor_encoder_get_buffer_size(&enc, buf) == 3 &&
          buf[0] == 0xF9 && buf[1] == 0x3E && buf[2] == 0, "half float 1.5 is F9 3E 00");
}

static void test_parse(void)
{
    CborParser parser;
    CborValue it, v, arr;
    int64_t i = 0;
    bool eq = 0;
    size_t len = 0;
    float f = 0;

    check(cbor_parser_init(doc, sizeof doc, 0, &parser, &it) == CborNoError, "parser init");
    check(cbor_value_validate_basic(&it) == CborNoError, "document validates");
    check(cbor_value_validate(&it, CborValidateStrictest) == CborNoError, "document validates strictly");
    check(cbor_value_is_map(&it), "root is a map");
    check(cbor_value_get_map_length(&it, &len) == CborNoError && len == 2, "two entries");

    check(cbor_value_map_find_value(&it, "a", &v) == CborNoError && cbor_value_is_integer(&v) &&
          cbor_value_get_int64(&v, &i) == CborNoError && i == 1, "a is 1");
    check(cbor_value_map_find_value(&it, "b", &v) == CborNoError && cbor_value_is_array(&v) &&
          cbor_value_enter_container(&v, &arr) == CborNoError && cbor_value_is_boolean(&arr) &&
          cbor_value_get_boolean(&arr, &eq) == CborNoError && eq &&
          cbor_value_advance_fixed(&arr) == CborNoError && cbor_value_is_null(&arr), "b is [true, null]");
    check(cbor_value_map_find_value(&it, "c", &v) == CborNoError && !cbor_value_is_valid(&v),
          "missing key");
    check(cbor_value_enter_container(&it, &v) == CborNoError &&
          cbor_value_text_string_equals(&v, "a", &eq) == CborNoError && eq, "first key is a");

    {
        static const uint8_t half[] = {0xF9, 0x3E, 0x00};
        check(cbor_parser_init(half, sizeof half, 0, &parser, &it) == CborNoError &&
              cbor_value_is_half_float(&it) &&
              cbor_value_get_half_float_as_float(&it, &f) == CborNoError && f == 1.5f,
              "half float decodes");
    }
    {
        static const uint8_t cut[] = {0xA2, 0x61, 0x61};
        check(cbor_parser_init(cut, sizeof cut, 0, &parser, &it) == CborNoError &&
              cbor_value_validate_basic(&it) == CborErrorUnexpectedEOF, "truncated map is rejected");
    }
    check(strlen(cbor_error_string(CborErrorUnexpectedEOF)) > 0, "error strings exist");
}

/* Reads back what a conversion wrote to the scratch file. */
static size_t slurp(FILE *f, char *out, size_t size)
{
    size_t n;
    rewind(f);
    n = fread(out, 1, size - 1, f);
    out[n] = '\0';
    return n;
}

static void test_text(void)
{
    CborParser parser;
    CborValue it;
    char text[128];
    FILE *f = tmpfile();

    if (!f) {
        check(0, "scratch file");
        return;
    }
    cbor_parser_init(doc, sizeof doc, 0, &parser, &it);
    check(cbor_value_to_json(f, &it, 0) == CborNoError, "to json");
    slurp(f, text, sizeof text);
    check(strcmp(text, "{\"a\":1,\"b\":[true,null]}") == 0, "json text");

    fclose(f);
    f = tmpfile();
    cbor_parser_init(doc, sizeof doc, 0, &parser, &it);
    check(cbor_value_to_pretty(f, &it) == CborNoError, "to diagnostic text");
    slurp(f, text, sizeof text);
    check(strcmp(text, "{\"a\": 1, \"b\": [true, null]}") == 0, "diagnostic text");

    {
        /* {"ab": 1}: multi-character text goes through the JSON escaper's size arithmetic. */
        static const uint8_t longkey[] = {0xA1, 0x62, 0x61, 0x62, 0x01};
        fclose(f);
        f = tmpfile();
        cbor_parser_init(longkey, sizeof longkey, 0, &parser, &it);
        check(cbor_value_to_json(f, &it, 0) == CborNoError, "to json, multi-character key");
        slurp(f, text, sizeof text);
        check(strcmp(text, "{\"ab\":1}") == 0, "multi-character key text");
    }

    {
        /* {1: 2}: a non-string key needs open_memstream to stringify. */
        static const uint8_t intkey[] = {0xA1, 0x01, 0x02};
        fclose(f);
        f = tmpfile();
        cbor_parser_init(intkey, sizeof intkey, 0, &parser, &it);
        check(cbor_value_to_json(f, &it, 0) == CborErrorJsonObjectKeyNotString, "int key refused");
        cbor_parser_init(intkey, sizeof intkey, 0, &parser, &it);
#ifdef _WIN32
        check(cbor_value_to_json(f, &it, CborConvertStringifyMapKeys) == CborErrorJsonNotImplemented,
              "no stringified keys on Windows");
#else
        check(cbor_value_to_json(f, &it, CborConvertStringifyMapKeys) == CborNoError, "stringify keys");
        slurp(f, text, sizeof text);
        check(strstr(text, "{\"1\":2}") != NULL, "stringified key text");
#endif
    }
    fclose(f);
}

int main(void)
{
    check(TINYCBOR_VERSION == ((7 << 16) | (0 << 8) | 0), "version header");
    test_encode();
    test_parse();
    test_text();
    return failures ? 1 : 0;
}
