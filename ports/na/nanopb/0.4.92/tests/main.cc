// Streams length-delimited messages through nanopb's callback streams into
// and out of a std::vector, binding the field lists with PB_BIND as a .pb.c
// file does: a negative int64 (a ten-byte varint), a bool, bytes, an enum,
// a double, and a oneof holding a submessage or a string.  The encoding is
// compared with hand-computed protobuf wire bytes, and a stream cut short
// fails to decode its last message.
#include <pb_decode.h>
#include <pb_encode.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

typedef enum { Unit_CELSIUS = 0, Unit_KELVIN = 1 } Unit;

struct Sample {
    double value;
    Unit unit;
};

typedef PB_BYTES_ARRAY_T(8) Reading_blob_t;

struct Reading {
    int64_t offset;
    bool valid;
    Reading_blob_t blob;
    pb_size_t which_payload;
    union {
        Sample sample;
        char note[12];
    } payload;
};

#define Sample_FIELDLIST(X, a) \
    X(a, STATIC, SINGULAR, DOUBLE, value, 1) \
    X(a, STATIC, SINGULAR, UENUM, unit, 2)
#define Sample_CALLBACK NULL
#define Sample_DEFAULT NULL

#define Reading_FIELDLIST(X, a) \
    X(a, STATIC, SINGULAR, INT64, offset, 1) \
    X(a, STATIC, SINGULAR, BOOL, valid, 2) \
    X(a, STATIC, SINGULAR, BYTES, blob, 3) \
    X(a, STATIC, ONEOF, MESSAGE, (payload,sample,payload.sample), 4) \
    X(a, STATIC, ONEOF, STRING, (payload,note,payload.note), 5)
#define Reading_CALLBACK NULL
#define Reading_DEFAULT NULL
#define Reading_payload_sample_MSGTYPE Sample

PB_BIND(Sample, Sample, AUTO)
PB_BIND(Reading, Reading, AUTO)

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "nanopb C++ test failed: %s\n", what);
        ++failures;
    }
}

bool write_vector(pb_ostream_t* stream, const pb_byte_t* buf, size_t count) {
    auto* out = static_cast<std::vector<pb_byte_t>*>(stream->state);
    out->insert(out->end(), buf, buf + count);
    return true;
}

struct Cursor {
    const std::vector<pb_byte_t>* bytes;
    size_t pos;
};

bool read_vector(pb_istream_t* stream, pb_byte_t* buf, size_t count) {
    auto* cursor = static_cast<Cursor*>(stream->state);
    std::memcpy(buf, cursor->bytes->data() + cursor->pos, count);
    cursor->pos += count;
    return true;
}

}  // namespace

int main() {
    Reading first{};
    first.offset = -5;
    first.valid = true;
    first.blob.size = 3;
    first.blob.bytes[0] = 0xDE;
    first.blob.bytes[1] = 0xAD;
    first.blob.bytes[2] = 0x01;
    first.which_payload = 4;
    first.payload.sample.value = 21.5;
    first.payload.sample.unit = Unit_KELVIN;

    // Proto3 omits the false bool and the empty bytes.
    Reading second{};
    second.offset = INT64_C(1) << 40;
    second.which_payload = 5;
    std::strcpy(second.payload.note, "hi");

    const std::vector<pb_byte_t> expected = {
        0x1F,                                                        // 31 bytes follow
        0x08, 0xFB, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x01,  // 1: -5
        0x10, 0x01,                                                  // 2: true
        0x1A, 0x03, 0xDE, 0xAD, 0x01,                                // 3: 3 bytes
        0x22, 0x0B,                                                  // 4: 11 bytes
        0x09, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x35, 0x40,        //   1: 21.5
        0x10, 0x01,                                                  //   2: KELVIN
        0x0B,                                                        // 11 bytes follow
        0x08, 0x80, 0x80, 0x80, 0x80, 0x80, 0x20,                    // 1: 2^40
        0x2A, 0x02, 'h', 'i',                                        // 5: "hi"
    };

    size_t size = 0;
    bool ok;
    check(pb_get_encoded_size(&size, &Reading_msg, &first) && size == 31, "pb_get_encoded_size");

    std::vector<pb_byte_t> bytes;
    pb_ostream_t ostream = {&write_vector, &bytes, SIZE_MAX, 0, nullptr};
    ok = pb_encode_delimited(&ostream, &Reading_msg, &first);
    check(ok, PB_GET_ERROR(&ostream));
    ok = pb_encode_delimited(&ostream, &Reading_msg, &second);
    check(ok, PB_GET_ERROR(&ostream));
    check(bytes == expected && ostream.bytes_written == expected.size(), "the wire bytes");

    Cursor cursor = {&expected, 0};
    pb_istream_t istream = {&read_vector, &cursor, expected.size(), nullptr};
    Reading a{};
    Reading b{};
    ok = pb_decode_delimited(&istream, &Reading_msg, &a);
    check(ok, PB_GET_ERROR(&istream));
    ok = pb_decode_delimited(&istream, &Reading_msg, &b);
    check(ok, PB_GET_ERROR(&istream));
    check(istream.bytes_left == 0 && cursor.pos == expected.size(), "the stream is consumed");
    check(a.offset == -5 && a.valid && a.blob.size == 3 && a.blob.bytes[0] == 0xDE &&
              a.blob.bytes[2] == 0x01 && a.which_payload == 4 &&
              a.payload.sample.value == 21.5 && a.payload.sample.unit == Unit_KELVIN,
          "the first message");
    check(b.offset == (INT64_C(1) << 40) && !b.valid && b.blob.size == 0 &&
              b.which_payload == 5 && std::strcmp(b.payload.note, "hi") == 0,
          "the second message");

    // The second message's length prefix promises one byte more than is left.
    cursor = {&expected, 0};
    istream = {&read_vector, &cursor, expected.size() - 1, nullptr};
    ok = pb_decode_delimited(&istream, &Reading_msg, &a);
    check(ok, PB_GET_ERROR(&istream));
    check(!pb_decode_delimited(&istream, &Reading_msg, &b) &&
              std::strcmp(PB_GET_ERROR(&istream), "parent stream too short") == 0,
          "a stream cut short");

    if (failures != 0)
        return 1;
    std::puts("nanopb C++ ok");
    return 0;
}
