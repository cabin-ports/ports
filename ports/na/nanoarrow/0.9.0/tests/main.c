/* The C consumer: the version the generated nanoarrow_config.h carries,
   a struct schema with field metadata and its string form, appending
   integers, strings, decimals and nulls row by row, reading them back
   through an ArrowArrayView, the validation levels, 128- and 256-bit
   decimal digits, reference-counted shared buffers and arrays, the basic
   array stream, and the errors for an unknown format, an out-of-range
   append and malformed digits. */
#include <nanoarrow/nanoarrow.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int ok = 1;

static void check(int pass, const char *what) {
    if (!pass) {
        fprintf(stderr, "FAIL: %s\n", what);
        ok = 0;
    }
}

/* A failed setup step leaves nothing meaningful to check after it. */
static void must(ArrowErrorCode code, const char *what) {
    if (code != NANOARROW_OK) {
        fprintf(stderr, "FAIL: %s returned %d\n", what, code);
        exit(1);
    }
}
#define MUST(expr) must((expr), #expr)

static int view_is(struct ArrowStringView view, const char *expected) {
    return view.size_bytes == (int64_t)strlen(expected) &&
           memcmp(view.data, expected, (size_t)view.size_bytes) == 0;
}

static int schema_string_is(const struct ArrowSchema *schema, char recursive,
                            const char *expected) {
    char out[128];
    int64_t n = ArrowSchemaToString(schema, out, sizeof(out), recursive);
    return n == (int64_t)strlen(expected) && strcmp(out, expected) == 0;
}

/* Renders a decimal with its scale, as "-0.75". */
static int decimal_string_is(const struct ArrowDecimal *decimal,
                             const char *expected) {
    struct ArrowBuffer buffer;
    int same;
    ArrowBufferInit(&buffer);
    MUST(ArrowDecimalAppendStringToBuffer(decimal, &buffer));
    same = buffer.size_bytes == (int64_t)strlen(expected) &&
           memcmp(buffer.data, expected, (size_t)buffer.size_bytes) == 0;
    ArrowBufferReset(&buffer);
    return same;
}

static void version(void) {
    check(strcmp(NANOARROW_VERSION, "0.9.0") == 0, "NANOARROW_VERSION");
    check(NANOARROW_VERSION_INT == 900, "NANOARROW_VERSION_INT");
    check(strcmp(ArrowNanoarrowVersion(), NANOARROW_VERSION) == 0,
          "the library's version matches its header");
    check(ArrowNanoarrowVersionInt() == NANOARROW_VERSION_INT,
          "the library's version number matches its header");
}

/* struct<id: int64, name: string, price: decimal128(10, 2)>, with the
   price field carrying {"currency": "EUR"} metadata. */
static void make_schema(struct ArrowSchema *schema) {
    struct ArrowBuffer metadata;

    ArrowSchemaInit(schema);
    MUST(ArrowSchemaSetTypeStruct(schema, 3));
    MUST(ArrowSchemaSetType(schema->children[0], NANOARROW_TYPE_INT64));
    MUST(ArrowSchemaSetName(schema->children[0], "id"));
    MUST(ArrowSchemaSetType(schema->children[1], NANOARROW_TYPE_STRING));
    MUST(ArrowSchemaSetName(schema->children[1], "name"));
    MUST(ArrowSchemaSetTypeDecimal(schema->children[2], NANOARROW_TYPE_DECIMAL128,
                                   10, 2));
    MUST(ArrowSchemaSetName(schema->children[2], "price"));

    MUST(ArrowMetadataBuilderInit(&metadata, NULL));
    MUST(ArrowMetadataBuilderAppend(&metadata, ArrowCharView("currency"),
                                    ArrowCharView("EUR")));
    MUST(ArrowSchemaSetMetadata(schema->children[2], (const char *)metadata.data));
    ArrowBufferReset(&metadata);
}

static void schemas(void) {
    struct ArrowSchema schema;
    struct ArrowSchema copy;
    struct ArrowSchemaView view;
    struct ArrowStringView currency;
    struct ArrowError error;

    make_schema(&schema);
    check(schema_string_is(&schema, 1,
                           "struct<id: int64, name: string, price: decimal128(10, 2)>"),
          "the recursive schema string");
    check(schema_string_is(&schema, 0, "struct"), "the top-level schema string");
    check(ArrowSchemaToString(&schema, NULL, 0, 1) == 57,
          "ArrowSchemaToString sizes its output without a buffer");

    MUST(ArrowSchemaViewInit(&view, schema.children[2], &error));
    check(view.type == NANOARROW_TYPE_DECIMAL128, "price is a decimal128");
    check(view.decimal_bitwidth == 128 && view.decimal_precision == 10 &&
              view.decimal_scale == 2,
          "price's width, precision and scale");

    MUST(ArrowMetadataGetValue(schema.children[2]->metadata, ArrowCharView("currency"),
                               &currency));
    check(view_is(currency, "EUR"), "price's currency metadata");
    check(ArrowMetadataHasKey(schema.children[2]->metadata, ArrowCharView("unit")) == 0,
          "price has no unit metadata");
    check(schema.children[0]->metadata == NULL, "id has no metadata");

    MUST(ArrowSchemaDeepCopy(&schema, &copy));
    ArrowSchemaRelease(&schema);
    check(schema.release == NULL, "ArrowSchemaRelease marks the schema released");
    check(schema_string_is(&copy, 1,
                           "struct<id: int64, name: string, price: decimal128(10, 2)>"),
          "the deep copy outlives its source");
    MUST(ArrowMetadataGetValue(copy.children[2]->metadata, ArrowCharView("currency"),
                               &currency));
    check(view_is(currency, "EUR"), "the deep copy keeps the metadata");
    ArrowSchemaRelease(&copy);
}

struct row {
    int64_t id;
    const char *name;    /* NULL appends a null */
    int64_t price_cents;
    int price_is_null;
};

static const struct row rows[] = {
    {1, "anchor", 1250, 0},
    {2, NULL, -75, 0},
    {3, "", 0, 1},
    /* An id beyond 32 bits and a two-byte UTF-8 character. */
    {40000000000LL, "caf\xc3\xa9", 9999999999LL, 0},
};
#define N_ROWS 4

static void append_rows(struct ArrowArray *array, const struct row *first, int n) {
    struct ArrowDecimal price;
    int i;

    MUST(ArrowArrayStartAppending(array));
    for (i = 0; i < n; i++) {
        const struct row *row = &first[i];
        MUST(ArrowArrayAppendInt(array->children[0], row->id));
        if (row->name != NULL) {
            MUST(ArrowArrayAppendString(array->children[1], ArrowCharView(row->name)));
        } else {
            MUST(ArrowArrayAppendNull(array->children[1], 1));
        }
        if (row->price_is_null) {
            MUST(ArrowArrayAppendNull(array->children[2], 1));
        } else {
            ArrowDecimalInit(&price, 128, 10, 2);
            ArrowDecimalSetInt(&price, row->price_cents);
            MUST(ArrowArrayAppendDecimal(array->children[2], &price));
        }
        MUST(ArrowArrayFinishElement(array));
    }
}

static void build_and_view(void) {
    struct ArrowSchema schema;
    struct ArrowArray array;
    struct ArrowArrayView view;
    struct ArrowDecimal price;
    struct ArrowError error;
    struct ArrowStringView name;

    make_schema(&schema);
    MUST(ArrowArrayInitFromSchema(&array, &schema, &error));
    append_rows(&array, rows, N_ROWS);
    MUST(ArrowArrayFinishBuildingDefault(&array, &error));

    check(array.length == N_ROWS && array.n_children == 3, "the struct's shape");
    check(array.children[0]->null_count == 0, "id has no nulls");
    check(array.children[1]->null_count == 1, "name has one null");
    check(array.children[2]->null_count == 1, "price has one null");

    MUST(ArrowArrayViewInitFromSchema(&view, &schema, &error));
    MUST(ArrowArrayViewSetArray(&view, &array, &error));
    MUST(ArrowArrayViewValidate(&view, NANOARROW_VALIDATION_LEVEL_FULL, &error));

    check(view.length == N_ROWS, "the view's length");
    check(ArrowArrayViewGetIntUnsafe(view.children[0], 0) == 1, "id[0]");
    check(ArrowArrayViewGetIntUnsafe(view.children[0], 3) == 40000000000LL,
          "id[3] keeps 64 bits");

    name = ArrowArrayViewGetStringUnsafe(view.children[1], 0);
    check(view_is(name, "anchor"), "name[0]");
    check(ArrowArrayViewIsNull(view.children[1], 1), "name[1] is null");
    check(!ArrowArrayViewIsNull(view.children[1], 2), "name[2] is not null");
    name = ArrowArrayViewGetStringUnsafe(view.children[1], 2);
    check(name.size_bytes == 0, "name[2] is empty");
    name = ArrowArrayViewGetStringUnsafe(view.children[1], 3);
    check(view_is(name, "caf\xc3\xa9"), "name[3] keeps its UTF-8 bytes");
    check(ArrowArrayViewComputeNullCount(view.children[1]) == 1,
          "the view counts name's nulls from its bitmap");

    ArrowDecimalInit(&price, 128, 10, 2);
    ArrowArrayViewGetDecimalUnsafe(view.children[2], 0, &price);
    check(decimal_string_is(&price, "12.50"), "price[0]");
    ArrowArrayViewGetDecimalUnsafe(view.children[2], 1, &price);
    check(decimal_string_is(&price, "-0.75"), "price[1] is negative");
    check(ArrowArrayViewIsNull(view.children[2], 2), "price[2] is null");
    ArrowArrayViewGetDecimalUnsafe(view.children[2], 3, &price);
    check(decimal_string_is(&price, "99999999.99"), "price[3] uses all ten digits");

    ArrowArrayViewReset(&view);
    ArrowArrayRelease(&array);
    ArrowSchemaRelease(&schema);
}

/* A string array with the given offsets over "abcde", left unvalidated. */
static void raw_strings(struct ArrowArray *array, const int32_t *offsets, int length) {
    struct ArrowError error;
    int i;

    MUST(ArrowArrayInitFromType(array, NANOARROW_TYPE_STRING));
    for (i = 0; i <= length; i++) {
        MUST(ArrowBufferAppendInt32(ArrowArrayBuffer(array, 1), offsets[i]));
    }
    MUST(ArrowBufferAppend(ArrowArrayBuffer(array, 2), "abcde", 5));
    array->length = length;
    array->null_count = 0;
    MUST(ArrowArrayFinishBuilding(array, NANOARROW_VALIDATION_LEVEL_NONE, &error));
}

static void validation(void) {
    static const int32_t decreasing[] = {0, 3, 1, 5};
    static const int32_t overrunning[] = {0, 2, 9};
    struct ArrowArray array;
    struct ArrowArrayView view;
    struct ArrowError error;

    /* Default validation checks only the first and last offsets; full
       validation also requires every element's size to be >= 0. */
    raw_strings(&array, decreasing, 3);
    ArrowArrayViewInitFromType(&view, NANOARROW_TYPE_STRING);
    check(ArrowArrayViewSetArray(&view, &array, &error) == NANOARROW_OK,
          "default validation accepts decreasing inner offsets");
    check(ArrowArrayViewValidate(&view, NANOARROW_VALIDATION_LEVEL_FULL, &error) ==
              EINVAL,
          "full validation rejects decreasing offsets");
    check(strcmp(ArrowErrorMessage(&error), "[2] Expected element size >= 0") == 0,
          "full validation names the offending element");
    ArrowArrayViewReset(&view);
    ArrowArrayRelease(&array);

    raw_strings(&array, overrunning, 2);
    check(ArrowArrayFinishBuilding(&array, NANOARROW_VALIDATION_LEVEL_DEFAULT, &error) ==
              EINVAL,
          "default validation rejects an offset past the data");
    check(strcmp(ArrowErrorMessage(&error),
                 "Expected string array buffer 2 to have size >= 9 bytes but found "
                 "buffer with 5 bytes") == 0,
          "default validation's message");
    ArrowArrayRelease(&array);
}

static void decimal_digits_round_trip(int bitwidth, const char *digits) {
    struct ArrowDecimal decimal;
    struct ArrowBuffer buffer;

    ArrowDecimalInit(&decimal, bitwidth, bitwidth == 128 ? 38 : 76, 0);
    MUST(ArrowDecimalSetDigits(&decimal, ArrowCharView(digits)));
    ArrowBufferInit(&buffer);
    MUST(ArrowDecimalAppendDigitsToBuffer(&decimal, &buffer));
    check(buffer.size_bytes == (int64_t)strlen(digits) &&
              memcmp(buffer.data, digits, (size_t)buffer.size_bytes) == 0,
          digits);
    ArrowBufferReset(&buffer);
}

static void decimals(void) {
    struct ArrowDecimal decimal;

    decimal_digits_round_trip(128, "-123456789012345678901234567890");
    decimal_digits_round_trip(128, "99999999999999999999999999999999999999");
    decimal_digits_round_trip(256,
                              "1234567890123456789012345678901234567890"
                              "123456789012345678901234567890");
    decimal_digits_round_trip(256, "-1");

    /* 2^64 sets exactly bit 0 of the high word. */
    ArrowDecimalInit(&decimal, 128, 38, 0);
    MUST(ArrowDecimalSetDigits(&decimal, ArrowCharView("18446744073709551616")));
    check(decimal.words[decimal.low_word_index] == 0 &&
              decimal.words[decimal.high_word_index] == 1,
          "2^64 spans two words");
    check(ArrowDecimalSign(&decimal) == 1, "2^64 is positive");

    /* A scale longer than the digits pads with zeros. */
    ArrowDecimalInit(&decimal, 128, 10, 4);
    ArrowDecimalSetInt(&decimal, -7);
    check(decimal_string_is(&decimal, "-0.0007"), "a small negative decimal");
    check(ArrowDecimalSign(&decimal) == -1, "-7 is negative");
}

static void shared_buffers(void) {
    static const int64_t values[] = {7, 8, 9};
    struct ArrowBuffer source;
    struct ArrowBuffer shared;
    struct ArrowBuffer clone;
    struct ArrowSharedArray shared_array;
    struct ArrowArray array;
    const uint8_t *data;
    int i;

    /* Upstream's default C99 build counts references without stdatomic. */
    check(ArrowSharedBufferIsThreadSafe() == 0,
          "shared buffers use plain reference counts");

    ArrowBufferInit(&source);
    MUST(ArrowBufferAppend(&source, "shared", 6));
    data = source.data;
    MUST(ArrowSharedBufferInit(&shared, &source));
    check(source.data == NULL && shared.data == data && ArrowIsSharedBuffer(&shared),
          "the shared buffer takes the source's bytes");
    check(ArrowSharedBufferClone(&source, &clone) == NANOARROW_OK && clone.data == NULL,
          "cloning an empty buffer yields an empty buffer");
    MUST(ArrowSharedBufferClone(&shared, &clone));
    ArrowBufferReset(&shared);
    check(clone.data == data && clone.size_bytes == 6 &&
              memcmp(clone.data, "shared", 6) == 0,
          "a clone keeps the bytes alive after the original's release");
    ArrowBufferReset(&clone);

    /* A buffer borrowed from a shared array outlives the array's handle. */
    MUST(ArrowArrayInitFromType(&array, NANOARROW_TYPE_INT64));
    MUST(ArrowArrayStartAppending(&array));
    for (i = 0; i < 3; i++) {
        MUST(ArrowArrayAppendInt(&array, values[i]));
    }
    MUST(ArrowArrayFinishBuildingDefault(&array, NULL));
    MUST(ArrowSharedArrayInit(&shared_array, &array));
    check(array.release == NULL, "the shared array takes the array");
    MUST(ArrowSharedArrayBuffer(&shared_array, 1, &shared));
    ArrowSharedArrayRelease(&shared_array);
    check(shared.size_bytes == (int64_t)sizeof(values) &&
              memcmp(shared.data, values, sizeof(values)) == 0,
          "the borrowed data buffer survives the shared array's release");
    ArrowBufferReset(&shared);
}

static void streams(void) {
    static const struct row more[] = {{5, "tail", 1, 0}};
    struct ArrowSchema schema;
    struct ArrowSchema got;
    struct ArrowArray first;
    struct ArrowArray second;
    struct ArrowArray next;
    struct ArrowArrayStream stream;
    struct ArrowError error;

    make_schema(&schema);
    MUST(ArrowArrayInitFromSchema(&first, &schema, &error));
    append_rows(&first, rows, N_ROWS);
    MUST(ArrowArrayFinishBuildingDefault(&first, &error));
    MUST(ArrowArrayInitFromSchema(&second, &schema, &error));
    append_rows(&second, more, 1);
    MUST(ArrowArrayFinishBuildingDefault(&second, &error));

    /* The stream takes the schema and both arrays. */
    MUST(ArrowBasicArrayStreamInit(&stream, &schema, 2));
    ArrowBasicArrayStreamSetArray(&stream, 0, &first);
    ArrowBasicArrayStreamSetArray(&stream, 1, &second);
    check(schema.release == NULL && first.release == NULL && second.release == NULL,
          "the stream owns its schema and arrays");
    MUST(ArrowBasicArrayStreamValidate(&stream, &error));

    MUST(ArrowArrayStreamGetSchema(&stream, &got, &error));
    check(schema_string_is(&got, 1,
                           "struct<id: int64, name: string, price: decimal128(10, 2)>"),
          "the stream's schema");
    ArrowSchemaRelease(&got);

    MUST(ArrowArrayStreamGetNext(&stream, &next, &error));
    check(next.release != NULL && next.length == N_ROWS, "the first batch");
    ArrowArrayRelease(&next);
    MUST(ArrowArrayStreamGetNext(&stream, &next, &error));
    check(next.release != NULL && next.length == 1, "the second batch");
    ArrowArrayRelease(&next);
    MUST(ArrowArrayStreamGetNext(&stream, &next, &error));
    check(next.release == NULL, "the stream ends after two batches");

    ArrowArrayStreamRelease(&stream);
    check(stream.release == NULL, "ArrowArrayStreamRelease releases the stream");
}

static void errors(void) {
    struct ArrowSchema schema;
    struct ArrowSchemaView view;
    struct ArrowArray array;
    struct ArrowDecimal decimal;
    struct ArrowError error;

    ArrowSchemaInit(&schema);
    MUST(ArrowSchemaSetFormat(&schema, "Q"));
    check(ArrowSchemaViewInit(&view, &schema, &error) == EINVAL,
          "an unknown format is rejected");
    check(strcmp(ArrowErrorMessage(&error),
                 "Error parsing schema->format: Unknown format: 'Q'") == 0,
          "the unknown format's message");
    check(schema_string_is(&schema, 1,
                           "[invalid: Error parsing schema->format: Unknown format: 'Q']"),
          "ArrowSchemaToString reports the invalid schema");
    ArrowSchemaRelease(&schema);

    MUST(ArrowArrayInitFromType(&array, NANOARROW_TYPE_INT8));
    MUST(ArrowArrayStartAppending(&array));
    MUST(ArrowArrayAppendInt(&array, -128));
    check(ArrowArrayAppendInt(&array, 128) == EINVAL, "int8 rejects 128");
    check(array.length == 1, "the rejected append adds nothing");
    ArrowArrayRelease(&array);

    ArrowDecimalInit(&decimal, 128, 38, 0);
    check(ArrowDecimalSetDigits(&decimal, ArrowCharView("12x4")) == EINVAL,
          "digits reject a letter");

    check(ArrowErrorSet(&error, "%s %d", "code", 7) == NANOARROW_OK &&
              strcmp(ArrowErrorMessage(&error), "code 7") == 0,
          "ArrowErrorSet formats its message");
}

int main(void) {
    version();
    schemas();
    build_and_view();
    validation();
    decimals();
    shared_buffers();
    streams();
    errors();
    printf("nanoarrow (C): %s\n", ok ? "ok" : "FAILED");
    return ok ? 0 : 1;
}
