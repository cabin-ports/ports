// Drives serialize's unified Serialize functions through WriteStream,
// ReadStream and MeasureStream.  The expected bytes come from upstream's
// conformance corpus (conformance/*.txt), whose vectors STANDARD.md derives
// without the C++ writer.  C++98 (C++14 with MSVC), as the probe compiles it.
#include <serialize.h>

#include <stdio.h>
#include <string.h>
#include <wchar.h>

namespace {

int failures = 0;

void fail(const char* stage, const char* what) {
    fprintf(stderr, "mas-bandwidth-serialize test failed: %s%s\n", stage, what);
    ++failures;
}

void check(bool ok, const char* what) {
    if (!ok)
        fail("", what);
}

// A write buffer is a multiple of 8 bytes, and a read needs 8 readable bytes
// past the data (README.md, "Limitations"); this size covers both.
enum { kBufferSize = 128 };

// Writes `message` and checks its bits and bytes, then reads those bytes back
// into `decoded` and checks that the read consumed the same bits.
template <typename Message>
void check_wire(Message& message, const uint8_t* bytes, int count, int64_t bits,
                Message& decoded, const char* what) {
    uint8_t buffer[kBufferSize];
    memset(buffer, 0, sizeof buffer);
    serialize::WriteStream writer(buffer, kBufferSize);
    const bool wrote = message.Serialize(writer);
    writer.Flush();
    if (!wrote || writer.GetBitsProcessed() != bits || writer.GetBytesProcessed() != count ||
        memcmp(buffer, bytes, count) != 0)
        fail("writing ", what);

    uint8_t input[kBufferSize];
    memset(input, 0, sizeof input);
    memcpy(input, bytes, count);
    serialize::ReadStream reader(input, count);
    if (!decoded.Serialize(reader) || reader.GetBitsProcessed() != bits)
        fail("reading ", what);
}

// message.txt, message-worked-example-wstring-align-fixed: the fragment of the
// golden message that STANDARD.md's worked example decodes.
struct WorkedExample {
    wchar_t text[8];
    int16_t level;  // Q8.8

    template <typename Stream>
    bool Serialize(Stream& stream) {
        serialize_wstring(stream, text, 8);
        serialize_align(stream);
        serialize_fixed(stream, level, 8, 8, -100, +100);
        return true;
    }
};

// sequence.txt, sequence-string-aligns-and-wstring-does-not.
struct Names {
    char name[16];
    wchar_t wide[8];

    template <typename Stream>
    bool Serialize(Stream& stream) {
        serialize_string(stream, name, 16);
        serialize_wstring(stream, wide, 8);
        return true;
    }
};

struct Relative {
    int previous;
    int current;

    template <typename Stream>
    bool Serialize(Stream& stream) {
        serialize_int_relative(stream, previous, current);
        return true;
    }
};

struct Throttle {
    float value;

    template <typename Stream>
    bool Serialize(Stream& stream) {
        serialize_compressed_float(stream, value, 0.0f, 10.0f, 0.01f);
        return true;
    }
};

struct EntityId {
    serialize::uint128_t value;

    template <typename Stream>
    bool Serialize(Stream& stream) {
        serialize_uint128(stream, value);
        return true;
    }
};

struct Offset {
    serialize::int128_t value;

    template <typename Stream>
    bool Serialize(Stream& stream) {
        const serialize::int128_t bound = serialize::int128_t(1) << 70;
        serialize_int128(stream, value, -bound, bound);
        return true;
    }
};

void test_conformance_vectors() {
    {
        static const uint8_t bytes[] = {0xE3, 0x21, 0x00, 0x00, 0xC0, 0x21, 0x00, 0x00,
                                        0x00, 0x22, 0x00, 0x00, 0x00, 0xC0, 0x60};
        WorkedExample message;
        memset(&message, 0, sizeof message);
        // Built from code points, so the source encoding cannot change them.
        const wchar_t mir[4] = {0x043C, 0x0438, 0x0440, 0};
        memcpy(message.text, mir, sizeof mir);
        message.level = -832;  // -3.25
        WorkedExample decoded;
        memset(&decoded, 0, sizeof decoded);
        check_wire(message, bytes, 15, 120, decoded, "the worked example");
        check(memcmp(decoded.text, mir, sizeof mir) == 0 && decoded.level == -832,
              "the worked example's values");
    }
    {
        static const uint8_t bytes[] = {0x03, 0x61, 0x62, 0x63, 0x09, 0x02, 0x00, 0x00, 0x00};
        Names message;
        memset(&message, 0, sizeof message);
        strcpy(message.name, "abc");
        message.wide[0] = 0x41;
        Names decoded;
        memset(&decoded, 0xFF, sizeof decoded);
        decoded.name[15] = '\0';  // bounds strcmp should the read fail
        check_wire(message, bytes, 9, 67, decoded, "a string then a wide string");
        check(strcmp(decoded.name, "abc") == 0 && decoded.wide[0] == 0x41 && decoded.wide[1] == 0,
              "the string aligns and the wide string does not");
    }
    {
        // int_relative.txt: one-bit-accept, bounded-9-accept-high, and
        // absolute-accept-domain-maximum, each relative to 100.
        static const uint8_t one_bit[] = {0x01};
        static const uint8_t bounded_9[] = {0x08, 0x10};
        static const uint8_t absolute[] = {0xC0, 0xFF, 0xFF, 0xFF, 0x1F};
        Relative message = {100, 101};
        Relative decoded = {100, 0};
        check_wire(message, one_bit, 1, 1, decoded, "a relative int one past the previous");
        check(decoded.current == 101, "the one-bit tier");
        message.current = 380;
        decoded.current = 0;
        check_wire(message, bounded_9, 2, 13, decoded, "a relative int in the 9-bit tier");
        check(decoded.current == 380, "the 9-bit tier");
        message.current = 2147483647;
        decoded.current = 0;
        check_wire(message, absolute, 5, 38, decoded, "a relative int at the domain maximum");
        check(decoded.current == 2147483647, "the absolute tier");
    }
    {
        // compressed_float.txt, compressed-float-on-quantum-anchor-over-0-10,
        // a reader vector; the write side holds because 5 quantizes exactly to
        // 500 = (5 / 10) * 1000.
        static const uint8_t bytes[] = {0xF4, 0x01};
        Throttle message = {5.0f};
        Throttle decoded = {0.0f};
        check_wire(message, bytes, 2, 10, decoded, "a compressed float");
        check(decoded.value == 5.0f, "the compressed float decodes to 5");
    }
    {
        // uint128.txt, uint128-distinct-halves: the low half goes first.
        static const uint8_t bytes[] = {0x10, 0x32, 0x54, 0x76, 0x98, 0xBA, 0xDC, 0xFE,
                                        0xEF, 0xCD, 0xAB, 0x89, 0x67, 0x45, 0x23, 0x01};
        EntityId message;
        message.value = (serialize::uint128_t(0x0123456789ABCDEFULL) << 64) |
                        serialize::uint128_t(0xFEDCBA9876543210ULL);
        EntityId decoded;
        decoded.value = 0;
        check_wire(message, bytes, 16, 128, decoded, "a 128-bit id");
        check(decoded.value == message.value, "the 128-bit id");
    }
    {
        // int128.txt, int128-72-bit-span-three-groups: a range of +-2^70
        // costs 72 bits.
        static const uint8_t bytes[] = {0x11, 0x32, 0x54, 0x76, 0x98, 0xBA, 0xDC, 0xFE, 0x3F};
        Offset message;
        message.value = -serialize::int128_t(0x0123456789ABCDEFLL);
        Offset decoded;
        decoded.value = 0;
        check_wire(message, bytes, 9, 72, decoded, "a ranged 128-bit int");
        check(decoded.value == message.value, "the ranged 128-bit int");
    }
}

// README.md's rigid body, extended over the remaining operations.
struct Vector {
    float x, y, z;

    template <typename Stream>
    bool Serialize(Stream& stream) {
        serialize_float(stream, x);
        serialize_float(stream, y);
        serialize_float(stream, z);
        return true;
    }
};

struct Body {
    uint32_t kind;
    int32_t health;
    bool at_rest;
    Vector position;
    Vector velocity;
    double mass;
    uint8_t team;
    uint16_t port;
    uint32_t seed;
    uint64_t tick;
    uint8_t token[5];
    char name[16];
    int64_t altitude;  // Q48.16

    template <typename Stream>
    bool Serialize(Stream& stream) {
        serialize_bits(stream, kind, 5);
        serialize_int(stream, health, -10, 100);
        serialize_bool(stream, at_rest);
        serialize_object(stream, position);
        if (!at_rest) {
            serialize_object(stream, velocity);
        } else if (Stream::IsReading) {
            velocity.x = velocity.y = velocity.z = 0.0f;
        }
        serialize_double(stream, mass);
        serialize_uint8(stream, team);
        serialize_uint16(stream, port);
        serialize_uint32(stream, seed);
        serialize_uint64(stream, tick);
        serialize_bytes(stream, token, 5);
        serialize_string(stream, name, 16);
        serialize_fixed(stream, altitude, 48, 16, -8192, +8192);
        return true;
    }
};

bool same(const Body& a, const Body& b) {
    return a.kind == b.kind && a.health == b.health && a.at_rest == b.at_rest &&
           a.position.x == b.position.x && a.position.y == b.position.y &&
           a.position.z == b.position.z && a.velocity.x == b.velocity.x &&
           a.velocity.y == b.velocity.y && a.velocity.z == b.velocity.z && a.mass == b.mass &&
           a.team == b.team && a.port == b.port && a.seed == b.seed && a.tick == b.tick &&
           memcmp(a.token, b.token, sizeof a.token) == 0 && strcmp(a.name, b.name) == 0 &&
           a.altitude == b.altitude;
}

// Returns the bits written.
int64_t round_trip(Body& body, const char* what) {
    serialize::MeasureStream measure;
    const bool measured = body.Serialize(measure);

    uint8_t buffer[kBufferSize];
    memset(buffer, 0, sizeof buffer);
    serialize::WriteStream writer(buffer, kBufferSize);
    const bool wrote = body.Serialize(writer);
    writer.Flush();
    if (!measured || !wrote || measure.GetBitsProcessed() < writer.GetBitsProcessed())
        fail("measuring and writing ", what);

    Body decoded;
    memset(&decoded, 0xFF, sizeof decoded);
    serialize::ReadStream reader(buffer, writer.GetBytesProcessed());
    if (!decoded.Serialize(reader) || reader.GetBitsProcessed() != writer.GetBitsProcessed() ||
        !same(body, decoded))
        fail("reading back ", what);
    return writer.GetBitsProcessed();
}

void test_round_trips() {
    Body body;
    memset(&body, 0, sizeof body);
    body.kind = 17;
    body.health = -3;
    body.at_rest = false;
    body.position.x = 1.5f;
    body.position.y = -2.25f;
    body.position.z = 1e-30f;
    body.velocity.x = 3.0f;
    body.velocity.y = 0.0f;
    body.velocity.z = -0.125f;
    body.mass = 1.0 / 3.0;
    body.team = 0xA5;
    body.port = 27015;
    body.seed = 0xDEADBEEFu;
    body.tick = 0x123456789ABCDEF0ULL;
    const uint8_t token[5] = {0x00, 0xFF, 0x10, 0x80, 0x7E};
    memcpy(body.token, token, sizeof token);
    strcpy(body.name, "rigid body");
    body.altitude = -(int64_t(4321) * 65536 + 12345);
    const int64_t moving = round_trip(body, "a moving body");

    body.at_rest = true;
    body.velocity.x = body.velocity.y = body.velocity.z = 0.0f;
    body.altitude = int64_t(8192) * 65536;
    const int64_t resting = round_trip(body, "a body at rest");
    // The fixed point field has the same width at any value, and the bytes
    // field aligns from 389 or 293 bits to the same padding.
    check(moving - resting == 3 * 32, "a body at rest skips its velocity");
}

struct Ranged {
    int32_t value;

    template <typename Stream>
    bool Serialize(Stream& stream) {
        serialize_int(stream, value, 0, 8);
        return true;
    }
};

struct Byte {
    uint32_t value;

    template <typename Stream>
    bool Serialize(Stream& stream) {
        serialize_bits(stream, value, 8);
        return true;
    }
};

struct Aligned {
    uint32_t value;

    template <typename Stream>
    bool Serialize(Stream& stream) {
        serialize_bits(stream, value, 3);
        serialize_align(stream);
        return true;
    }
};

struct Name {
    char text[16];

    template <typename Stream>
    bool Serialize(Stream& stream) {
        serialize_string(stream, text, 16);
        return true;
    }
};

// Reads `count` bytes of hostile input from a padded buffer.
struct Input {
    uint8_t bytes[kBufferSize];

    Input(const uint8_t* data, int count) {
        memset(bytes, 0, sizeof bytes);
        memcpy(bytes, data, count);
    }
};

void test_refusals() {
    {
        // sequence.txt, sequence-refusal-is-terminal-after-a-range-refusal:
        // offset 9 is past [0,8], and the stream stays failed although a byte
        // remains.
        static const uint8_t bytes[] = {0x09, 0xAB};
        Input input(bytes, 2);
        serialize::ReadStream reader(input.bytes, 2);
        Ranged ranged = {7};
        Byte byte = {7};
        check(!ranged.Serialize(reader) && ranged.value == 7,
              "a value out of range is refused and leaves the destination alone");
        check(!byte.Serialize(reader) && byte.value == 7, "a refusal is terminal");

        static const uint8_t good[] = {0x08, 0xAB};
        Input fresh(good, 2);
        reader.Initialize(fresh.bytes, 2);
        check(ranged.Serialize(reader) && ranged.value == 8, "Initialize clears a failed stream");
    }
    {
        // sequence.txt, sequence-refusal-is-terminal-after-bad-alignment.
        static const uint8_t bytes[] = {0xFD, 0xAB};
        Input input(bytes, 2);
        serialize::ReadStream reader(input.bytes, 2);
        Aligned aligned = {0};
        Byte byte = {7};
        check(!aligned.Serialize(reader), "non-zero alignment padding is refused");
        check(!byte.Serialize(reader) && byte.value == 7, "a refusal at an align is terminal");
    }
    {
        // string.txt: string-refuse-nul-in-the-middle and
        // string-refuse-overlong-two-byte-nul.
        static const uint8_t interior_nul[] = {0x03, 0x61, 0x00, 0x62};
        static const uint8_t overlong[] = {0x02, 0xC0, 0x80};
        Name name;
        Input first(interior_nul, 4);
        serialize::ReadStream reader(first.bytes, 4);
        check(!name.Serialize(reader), "a string with an interior NUL is refused");
        Input second(overlong, 3);
        reader.Initialize(second.bytes, 3);
        check(!name.Serialize(reader), "an overlong UTF-8 encoding is refused");
    }
    {
        // int_relative.txt, one-bit-refuse-past-domain: 2^31 is past the
        // domain.
        static const uint8_t bytes[] = {0x01};
        Input input(bytes, 1);
        serialize::ReadStream reader(input.bytes, 1);
        Relative relative = {2147483647, 5};
        check(!relative.Serialize(reader) && relative.current == 5,
              "a relative int past 2^31 - 1 is refused");
    }
    {
        // compressed_float.txt,
        // compressed-float-refuse-integer-one-past-max-integer-value.
        static const uint8_t bytes[] = {0xE9, 0x03};
        Input input(bytes, 2);
        serialize::ReadStream reader(input.bytes, 2);
        Throttle throttle = {1.0f};
        check(!throttle.Serialize(reader) && throttle.value == 1.0f,
              "a compressed float past its range is refused");
    }
    {
        // A packet in an exactly sized allocation is read through
        // InitializePadded, which copies it next to 8 zeroed bytes.
        const uint8_t packet[2] = {0x05, 0xAB};
        uint8_t destination[2 + 8];
        serialize::ReadStream reader;
        Aligned aligned = {0};
        Byte byte = {0};
        check(reader.InitializePadded(destination, sizeof destination, packet, 2) &&
                  aligned.Serialize(reader) && aligned.value == 5 && byte.Serialize(reader) &&
                  byte.value == 0xAB,
              "InitializePadded reads an exactly sized packet");
    }
}

}  // namespace

int main() {
    check(strcmp(SERIALIZE_VERSION, "1.17.0") == 0 && SERIALIZE_VERSION_MAJOR == 1 &&
              SERIALIZE_VERSION_MINOR == 17,
          "SERIALIZE_VERSION");

    // After README.md's bit packer example.
    uint8_t buffer[256];
    memset(buffer, 0, sizeof buffer);
    serialize::BitWriter writer(buffer, sizeof buffer);
    writer.WriteBits(0, 1);
    writer.WriteBits(1, 1);
    writer.WriteBits(10, 8);
    writer.WriteBits(1000, 10);
    writer.WriteBits(9999999, 32);
    writer.FlushBits();
    check(writer.GetBitsWritten() == 52 && writer.GetBytesWritten() == 7, "BitWriter counts");
    serialize::BitReader reader(buffer, writer.GetBytesWritten());
    check(reader.ReadBits(1) == 0 && reader.ReadBits(1) == 1 && reader.ReadBits(8) == 10 &&
              reader.ReadBits(10) == 1000 && reader.ReadBits(32) == 9999999 &&
              reader.GetBitsRemaining() == 4,
          "BitReader reads back what BitWriter wrote");

    test_conformance_vectors();
    test_round_trips();
    test_refusals();

    if (failures != 0)
        return 1;
    printf("mas-bandwidth-serialize %s ok\n", SERIALIZE_VERSION);
    return 0;
}
