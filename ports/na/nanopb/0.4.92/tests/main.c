/* Encodes a message with the nanopb runtime and compares the protobuf wire
   bytes with hand-computed ones, then decodes them back: a string, a
   repeated submessage with zigzag-encoded sint32 fields, an optional
   fixed32, a repeated string through callbacks, and a uint64 varint.  The
   field lists take the form nanopb's generator writes into .pb.h files and
   are bound with PB_BIND, as its .pb.c files do.  Decoding also resets
   fields the input omits, skips an unknown field, and rejects a truncated
   message and an over-long string. */
#include <pb_decode.h>
#include <pb_encode.h>

#include <stdio.h>
#include <string.h>

typedef struct {
    int32_t x;
    int32_t y;
} Point;

typedef struct {
    char name[16];
    pb_size_t points_count;
    Point points[4];
    bool has_color;
    uint32_t color;
    pb_callback_t tags;
    uint64_t id;
} Shape;

#define Point_FIELDLIST(X, a) \
    X(a, STATIC, SINGULAR, SINT32, x, 1) \
    X(a, STATIC, SINGULAR, SINT32, y, 2)
#define Point_CALLBACK NULL
#define Point_DEFAULT NULL

#define Shape_FIELDLIST(X, a) \
    X(a, STATIC, SINGULAR, STRING, name, 1) \
    X(a, STATIC, REPEATED, MESSAGE, points, 2) \
    X(a, STATIC, OPTIONAL, FIXED32, color, 3) \
    X(a, CALLBACK, REPEATED, STRING, tags, 4) \
    X(a, STATIC, SINGULAR, UINT64, id, 5)
#define Shape_CALLBACK pb_default_field_callback
#define Shape_DEFAULT NULL
#define Shape_points_MSGTYPE Point

PB_BIND(Point, Point, AUTO)
PB_BIND(Shape, Shape, AUTO)

typedef struct {
    int count;
    char items[4][8];
} TagList;

static int failures = 0;

static void check(int ok, const char* what) {
    if (!ok) {
        fprintf(stderr, "nanopb C test failed: %s\n", what);
        ++failures;
    }
}

static bool encode_tags(pb_ostream_t* stream, const pb_field_t* field, void* const* arg) {
    const char* const* tag;
    for (tag = (const char* const*)*arg; *tag != NULL; ++tag) {
        if (!pb_encode_tag_for_field(stream, field) ||
            !pb_encode_string(stream, (const pb_byte_t*)*tag, strlen(*tag)))
            return false;
    }
    return true;
}

/* Called once per occurrence of the field, with a substream over its bytes. */
static bool decode_tag(pb_istream_t* stream, const pb_field_t* field, void** arg) {
    TagList* list = (TagList*)*arg;
    size_t length = stream->bytes_left;
    (void)field;
    if (list->count == 4 || length >= sizeof list->items[0])
        PB_RETURN_ERROR(stream, "tag too long");
    if (!pb_read(stream, (pb_byte_t*)list->items[list->count], length))
        return false;
    list->items[list->count++][length] = '\0';
    return true;
}

int main(void) {
    static const pb_byte_t expected[] = {
        0x0A, 0x03, 't', 'r', 'i',             /* 1: "tri" */
        0x12, 0x04, 0x08, 0x02, 0x10, 0x03,    /* 2: {x: 1, y: -2} */
        0x12, 0x04, 0x08, 0x06, 0x10, 0x08,    /* 2: {x: 3, y: 4} */
        0x1D, 0x00, 0xFF, 0x00, 0xFF,          /* 3: 0xFF00FF00, little-endian */
        0x22, 0x01, 'a', 0x22, 0x02, 'b', 'c', /* 4: "a", "bc" */
        0x28, 0xAC, 0x02,                      /* 5: 300 */
    };
    /* An unknown field 15 (varint 1), then id 7. */
    static const pb_byte_t sparse[] = {0x78, 0x01, 0x28, 0x07};
    static const char* const tags[] = {"a", "bc", NULL};
    pb_byte_t buffer[64];
    pb_byte_t long_name[18];
    pb_ostream_t ostream = pb_ostream_from_buffer(buffer, sizeof buffer);
    pb_ostream_t small = pb_ostream_from_buffer(buffer, 10);
    pb_istream_t istream;
    Shape out;
    Shape in;
    TagList list;
    size_t size = 0;
    bool ok;

    memset(&out, 0, sizeof out);
    strcpy(out.name, "tri");
    out.points_count = 2;
    out.points[0].x = 1;
    out.points[0].y = -2;
    out.points[1].x = 3;
    out.points[1].y = 4;
    out.has_color = true;
    out.color = 0xFF00FF00u;
    out.tags.funcs.encode = encode_tags;
    out.tags.arg = (void*)tags;
    out.id = 300;

    check(pb_get_encoded_size(&size, &Shape_msg, &out) && size == sizeof expected,
          "pb_get_encoded_size");
    ok = pb_encode(&ostream, &Shape_msg, &out);
    check(ok, PB_GET_ERROR(&ostream));
    check(ostream.bytes_written == sizeof expected &&
              memcmp(buffer, expected, sizeof expected) == 0,
          "the wire bytes");

    memset(&in, 0, sizeof in);
    memset(&list, 0, sizeof list);
    in.tags.funcs.decode = decode_tag;
    in.tags.arg = &list;
    istream = pb_istream_from_buffer(expected, sizeof expected);
    ok = pb_decode(&istream, &Shape_msg, &in);
    check(ok, PB_GET_ERROR(&istream));
    check(strcmp(in.name, "tri") == 0 && in.points_count == 2 && in.points[0].x == 1 &&
              in.points[0].y == -2 && in.points[1].x == 3 && in.points[1].y == 4 &&
              in.has_color && in.color == 0xFF00FF00u && in.id == 300 &&
              istream.bytes_left == 0,
          "the decoded fields");
    check(list.count == 2 && strcmp(list.items[0], "a") == 0 && strcmp(list.items[1], "bc") == 0,
          "the decoded tags");

    /* pb_decode first resets the static fields to their defaults. */
    istream = pb_istream_from_buffer(sparse, sizeof sparse);
    ok = pb_decode(&istream, &Shape_msg, &in);
    check(ok, PB_GET_ERROR(&istream));
    check(in.name[0] == '\0' && in.points_count == 0 && !in.has_color && in.id == 7 &&
              list.count == 2,
          "a sparse message");

    check(!pb_encode(&small, &Shape_msg, &out) && strcmp(PB_GET_ERROR(&small), "stream full") == 0,
          "a full buffer");

    istream = pb_istream_from_buffer(expected, sizeof expected - 1);
    /* The last varint's continuation bit promises a byte that is not there. */
    check(!pb_decode(&istream, &Shape_msg, &in) &&
              strcmp(PB_GET_ERROR(&istream), "end-of-stream") == 0,
          "a truncated message");

    /* Sixteen characters do not fit name[16] with its terminator. */
    long_name[0] = 0x0A;
    long_name[1] = 16;
    memset(long_name + 2, 'x', 16);
    istream = pb_istream_from_buffer(long_name, sizeof long_name);
    check(!pb_decode(&istream, &Shape_msg, &in) &&
              strcmp(PB_GET_ERROR(&istream), "string overflow") == 0,
          "an over-long string");

    if (failures != 0)
        return 1;
    puts("nanopb C ok");
    return 0;
}
