// Encodes one struct with Fast CDR as XCDRv1 and XCDRv2 in both byte
// orders, checks the bytes of two of the streams, and decodes all four.
#include <fastcdr/Cdr.h>
#include <fastcdr/FastBuffer.h>
#include <fastcdr/config.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace fcdr = eprosima::fastcdr;

static_assert(FASTCDR_SIZEOF_LONG_DOUBLE == sizeof(long double),
              "config.h states the size of long double");

struct Sample
{
    bool flag;
    std::int32_t count;
    std::uint64_t stamp;
    std::string name;
    double ratio;
    std::wstring label;
    std::vector<std::int16_t> deltas;
    std::array<std::uint16_t, 2> pair;
    long double precise;
};

namespace {

// An @appendable struct: delimited (DHEADER) under XCDRv2, plain under XCDRv1.
fcdr::EncodingAlgorithmFlag encoding_of(const fcdr::Cdr& cdr)
{
    return cdr.get_cdr_version() == fcdr::XCDRv2 ? fcdr::DELIMIT_CDR2 : fcdr::PLAIN_CDR;
}

} // namespace

namespace eprosima {
namespace fastcdr {

template<>
void serialize(Cdr& cdr, const Sample& s)
{
    Cdr::state state(cdr);
    cdr.begin_serialize_type(state, encoding_of(cdr));
    cdr << MemberId(0) << s.flag;
    cdr << MemberId(1) << s.count;
    cdr << MemberId(2) << s.stamp;
    cdr << MemberId(3) << s.name;
    cdr << MemberId(4) << s.ratio;
    cdr << MemberId(5) << s.label;
    cdr << MemberId(6) << s.deltas;
    cdr << MemberId(7) << s.pair;
    cdr << MemberId(8) << s.precise;
    cdr.end_serialize_type(state);
}

template<>
void deserialize(Cdr& cdr, Sample& s)
{
    cdr.deserialize_type(encoding_of(cdr), [&s](Cdr& in, const MemberId& mid) -> bool
            {
                switch (mid.id)
                {
                    case 0: in >> s.flag; break;
                    case 1: in >> s.count; break;
                    case 2: in >> s.stamp; break;
                    case 3: in >> s.name; break;
                    case 4: in >> s.ratio; break;
                    case 5: in >> s.label; break;
                    case 6: in >> s.deltas; break;
                    case 7: in >> s.pair; break;
                    case 8: in >> s.precise; break;
                    default: return false;
                }
                return true;
            });
}

} // namespace fastcdr
} // namespace eprosima

namespace {

int failures = 0;

void check(bool ok, const char* what, const char* stream)
{
    if (!ok)
    {
        std::printf("FAIL %s: %s\n", stream, what);
        ++failures;
    }
}

// XCDRv1 aligns 8-byte values to 8, relative to the end of the 4-byte
// encapsulation header; padding is never written, so it stays 0.
const unsigned char xcdr1_be[] = {
    0x00, 0x00, 0x00, 0x00,                         // CDR_BE, options 0
    0x01, 0x00, 0x00, 0x00,                         // bool, pad to 4
    0xFF, 0xFF, 0xFF, 0xFE,                         // int32 -2
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, // uint64
    0x00, 0x00, 0x00, 0x03, 'h', 'i', 0x00,         // string: length with NUL
    0x00,                                           // pad to 8
    0x3F, 0xF8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // double 1.5
    0x00, 0x00, 0x00, 0x02, 0x00, 'a', 0x00, 'b',   // wstring: count, 16-bit units
    0x00, 0x00, 0x00, 0x03,                         // sequence<int16> length
    0x00, 0x01, 0xFF, 0xFE, 0x00, 0x03,
    0xAB, 0xCD, 0x01, 0x02,                         // uint16[2]
    0x00, 0x00,                                     // pad to 8
    // long double, 16 bytes: zeros, then the 8-byte value
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x3F, 0xF8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

// XCDRv2 aligns 8-byte values to 4; the DHEADER counts the bytes after it.
const unsigned char xcdr2_le[] = {
    0x00, 0x09, 0x00, 0x00,                         // D_CDR2_LE, options 0
    0x48, 0x00, 0x00, 0x00,                         // DHEADER 72
    0x01, 0x00, 0x00, 0x00,                         // bool, pad to 4
    0xFE, 0xFF, 0xFF, 0xFF,                         // int32 -2
    0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01, // uint64
    0x03, 0x00, 0x00, 0x00, 'h', 'i', 0x00,         // string: length with NUL
    0x00,                                           // pad to 4
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF8, 0x3F, // double 1.5
    0x02, 0x00, 0x00, 0x00, 'a', 0x00, 'b', 0x00,   // wstring: count, 16-bit units
    0x03, 0x00, 0x00, 0x00,                         // sequence<int16> length
    0x01, 0x00, 0xFE, 0xFF, 0x03, 0x00,
    0xCD, 0xAB, 0x02, 0x01,                         // uint16[2]
    0x00, 0x00,                                     // pad to 4
    // long double, 16 bytes: zeros, then the 8-byte value
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF8, 0x3F,
};

void run(const Sample& sample, fcdr::CdrVersion version, fcdr::Cdr::Endianness endianness,
        const unsigned char* expected, std::size_t expected_size, const char* stream)
{
    char buffer[128] = {};
    fcdr::FastBuffer out_buffer(buffer, sizeof(buffer));
    fcdr::Cdr out(out_buffer, endianness, version);
    const fcdr::EncodingAlgorithmFlag flag = encoding_of(out);
    out.set_encoding_flag(flag);
    out.serialize_encapsulation();
    out << sample;

    const std::size_t length = out.get_serialized_data_length();
    check(length == (version == fcdr::XCDRv2 ? 80u : 76u), "stream length", stream);
    check(buffer[0] == 0 && buffer[1] == (flag | endianness) && buffer[2] == 0 && buffer[3] == 0,
            "encapsulation header", stream);
    if (expected != nullptr && length == expected_size)
    {
        // The long double's bytes are the host's format; they are known only
        // where it is an 8-byte double that is not widened to __float128.
        const bool ld_known = sizeof(long double) == 8 && !FASTCDR_HAVE_FLOAT128;
        const std::size_t known = ld_known ? length : length - 16;
        check(std::memcmp(buffer, expected, known) == 0, "encoded bytes", stream);
    }

    fcdr::FastBuffer in_buffer(buffer, length);
    fcdr::Cdr in(in_buffer);
    in.read_encapsulation();
    check(in.get_cdr_version() == version, "decoded version", stream);
    check(in.get_encoding_flag() == flag, "decoded encoding", stream);
    check(in.endianness() == endianness, "decoded byte order", stream);

    Sample got = Sample();
    in >> got;
    check(in.get_serialized_data_length() == length, "decoded length", stream);
    check(got.flag == sample.flag, "bool", stream);
    check(got.count == sample.count, "int32", stream);
    check(got.stamp == sample.stamp, "uint64", stream);
    check(got.name == sample.name, "string", stream);
    check(got.ratio == sample.ratio, "double", stream);
    check(got.label == sample.label, "wstring", stream);
    check(got.deltas == sample.deltas, "sequence", stream);
    check(got.pair == sample.pair, "array", stream);
    check(got.precise == sample.precise, "long double", stream);
}

} // namespace

int main()
{
    check(std::strcmp(FASTCDR_VERSION_STR, "2.5.0") == 0 && FASTCDR_VERSION_MAJOR == 2 &&
            FASTCDR_VERSION_MINOR == 5 && FASTCDR_VERSION_MICRO == 0, "version", "config.h");
    const std::uint16_t one = 1;
    unsigned char first_byte = 0;
    std::memcpy(&first_byte, &one, 1);
    check(fcdr::Cdr::DEFAULT_ENDIAN ==
            (first_byte == 1 ? fcdr::Cdr::LITTLE_ENDIANNESS : fcdr::Cdr::BIG_ENDIANNESS),
            "DEFAULT_ENDIAN is the host byte order", "config.h");

    Sample sample;
    sample.flag = true;
    sample.count = -2;
    sample.stamp = 0x0102030405060708ULL;
    sample.name = "hi";
    sample.ratio = 1.5;
    sample.label = L"ab";
    sample.deltas = {1, -2, 3};
    sample.pair = {{0xABCD, 0x0102}};
    sample.precise = 1.5L;

    try
    {
        run(sample, fcdr::XCDRv1, fcdr::Cdr::BIG_ENDIANNESS, xcdr1_be, sizeof(xcdr1_be), "XCDRv1 BE");
        run(sample, fcdr::XCDRv1, fcdr::Cdr::LITTLE_ENDIANNESS, nullptr, 0, "XCDRv1 LE");
        run(sample, fcdr::XCDRv2, fcdr::Cdr::BIG_ENDIANNESS, nullptr, 0, "XCDRv2 BE");
        run(sample, fcdr::XCDRv2, fcdr::Cdr::LITTLE_ENDIANNESS, xcdr2_le, sizeof(xcdr2_le), "XCDRv2 LE");
    }
    catch (const fcdr::exception::Exception& e)
    {
        std::printf("FAIL exception: %s\n", e.what());
        return 1;
    }

    if (failures != 0)
    {
        return 1;
    }
    std::printf("fastcdr: XCDRv1 and XCDRv2 streams encoded and decoded\n");
    return 0;
}
