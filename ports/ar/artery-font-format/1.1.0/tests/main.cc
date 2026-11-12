// Builds a font with artery-font's std::vector and std::string bindings,
// encodes it through a WriteFunction into memory, checks the header fields,
// lengths and CRC-32 the format defines, decodes it back through a
// ReadFunction, and round-trips it through a file with the stdio helpers.
// Damaged or mismatched input must be refused.  C++11 (C++14 with MSVC), as
// the probe compiles it.
#include <artery-font/std-artery-font.h>
#include <artery-font/stdio-serialization.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace af = artery_font;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "artery-font-format test failed: %s\n", what);
        ++failures;
    }
}

typedef std::vector<unsigned char> Bytes;

struct Sink {
    Bytes bytes;
    size_t limit;
};

// A WriteFunction that accepts at most `limit` bytes in all.
int sink_write(const void* src, int length, void* user) {
    Sink* sink = static_cast<Sink*>(user);
    size_t n = static_cast<size_t>(length);
    if (n > sink->limit - sink->bytes.size())
        n = sink->limit - sink->bytes.size();
    const unsigned char* p = static_cast<const unsigned char*>(src);
    sink->bytes.insert(sink->bytes.end(), p, p + n);
    return static_cast<int>(n);
}

struct Source {
    const Bytes* bytes;
    size_t position;
};

int source_read(void* dst, int limit, void* user) {
    Source* source = static_cast<Source*>(user);
    size_t n = source->bytes->size() - source->position;
    if (n > static_cast<size_t>(limit))
        n = static_cast<size_t>(limit);
    if (n != 0)
        std::memcpy(dst, source->bytes->data() + source->position, n);
    source->position += n;
    return static_cast<int>(n);
}

template <typename REAL>
bool encode(const af::StdArteryFont<REAL>& font, Bytes& out, size_t limit = 1 << 20) {
    Sink sink = {Bytes(), limit};
    const bool ok = af::encode<sink_write>(font, &sink);
    out.swap(sink.bytes);
    return ok;
}

template <typename REAL>
bool decode(af::StdArteryFont<REAL>& font, const Bytes& in) {
    Source source = {&in, 0};
    return af::decode<source_read>(font, &source);
}

uint32_t le32(const Bytes& b, size_t at) {
    return uint32_t(b[at]) | uint32_t(b[at + 1]) << 8 | uint32_t(b[at + 2]) << 16 |
           uint32_t(b[at + 3]) << 24;
}

// The reflected CRC-32 of zlib and PNG, computed bit by bit.
uint32_t crc32(const unsigned char* p, size_t n) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; ++i) {
        crc ^= p[i];
        for (int k = 0; k < 8; ++k)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

af::StdString text(const char* s) {
    return af::StdString(s, static_cast<int>(std::strlen(s)));
}

bool same(const af::StdString& a, const char* b) {
    return a.length() == static_cast<int>(std::strlen(b)) &&
           std::strcmp(static_cast<const char*>(a), b) == 0;
}

af::StdByteArray bytes(const unsigned char* p, int n) {
    af::StdByteArray array(n);
    std::memcpy(static_cast<unsigned char*>(array), p, n);
    return array;
}

const unsigned char kPixels[6] = {0, 64, 128, 192, 255, 7};
const unsigned char kAppendix[5] = {1, 2, 3, 4, 5};

// One bold MSDF variant with two glyphs and a kerning pair, a 3x2 8-bit
// raw image, and an appendix.  The string and data lengths are not multiples
// of 4, so every padding rule applies.
template <typename REAL>
void fill(af::StdArteryFont<REAL>& font) {
    typedef af::StdArteryFont<REAL> Font;
    font.metadataFormat = af::METADATA_JSON;
    font.metadata = text("{\"family\":\"Probe\"}");

    font.variants = af::StdList<typename Font::Variant>(1);
    typename Font::Variant& v = font.variants[0];
    v.flags = af::FONT_BOLD | af::FONT_SANS_SERIF;
    v.weight = 700;
    v.codepointType = af::CP_UNICODE;
    v.imageType = af::IMAGE_MSDF;
    v.fallbackVariant = 0;
    v.fallbackGlyph = '?';
    std::memset(&v.metrics, 0, sizeof v.metrics);
    v.metrics.fontSize = REAL(32);
    v.metrics.distanceRange = REAL(4);
    v.metrics.emSize = REAL(1);
    v.metrics.ascender = REAL(0.75);
    v.metrics.descender = REAL(-0.25);
    v.metrics.lineHeight = REAL(1.25);
    v.metrics.underlineY = REAL(-0.125);
    v.metrics.underlineThickness = REAL(0.0625);
    v.name = text("Probe Bold");
    v.glyphs = af::StdList<af::Glyph<REAL> >(2);
    for (int i = 0; i < 2; ++i) {
        af::Glyph<REAL>& g = v.glyphs[i];
        g.codepoint = i == 0 ? 'A' : 'V';
        g.image = 0;
        g.planeBounds.l = REAL(0);
        g.planeBounds.b = REAL(-0.125 * i);
        g.planeBounds.r = REAL(0.5);
        g.planeBounds.t = REAL(0.75);
        g.imageBounds.l = REAL(16 * i);
        g.imageBounds.b = REAL(0);
        g.imageBounds.r = REAL(16 * i + 16);
        g.imageBounds.t = REAL(24);
        g.advance.h = REAL(0.625);
        g.advance.v = REAL(0);
    }
    v.kernPairs = af::StdList<af::KernPair<REAL> >(1);
    v.kernPairs[0].codepoint1 = 'A';
    v.kernPairs[0].codepoint2 = 'V';
    v.kernPairs[0].advance.h = REAL(-0.0625);
    v.kernPairs[0].advance.v = REAL(0);

    font.images = af::StdList<typename Font::Image>(1);
    typename Font::Image& image = font.images[0];
    image.flags = 0;
    image.encoding = af::IMAGE_RAW_BINARY;
    image.width = 3;
    image.height = 2;
    image.channels = 1;
    image.pixelFormat = af::PIXEL_UNSIGNED8;
    image.imageType = af::IMAGE_SDF;
    image.rawBinaryFormat.rowLength = 3;
    image.rawBinaryFormat.orientation = af::ORIENTATION_BOTTOM_UP;
    image.childImages = 0;
    image.textureFlags = 0;
    image.metadata = text("sdf");
    image.data = bytes(kPixels, 6);

    font.appendices = af::StdList<typename Font::Appendix>(1);
    font.appendices[0].metadata = text("notes");
    font.appendices[0].data = bytes(kAppendix, 5);
}

template <typename REAL>
bool same(const af::StdArteryFont<REAL>& a, const af::StdArteryFont<REAL>& b) {
    if (a.metadataFormat != b.metadataFormat ||
        !same(a.metadata, static_cast<const char*>(b.metadata)) ||
        a.variants.length() != 1 || b.variants.length() != 1 || a.images.length() != 1 ||
        b.images.length() != 1 || a.appendices.length() != 1 || b.appendices.length() != 1)
        return false;
    const typename af::StdArteryFont<REAL>::Variant& va = a.variants[0];
    const typename af::StdArteryFont<REAL>::Variant& vb = b.variants[0];
    if (va.flags != vb.flags || va.weight != vb.weight || va.codepointType != vb.codepointType ||
        va.imageType != vb.imageType || va.fallbackVariant != vb.fallbackVariant ||
        va.fallbackGlyph != vb.fallbackGlyph ||
        std::memcmp(&va.metrics, &vb.metrics, sizeof va.metrics) != 0 ||
        !same(va.name, static_cast<const char*>(vb.name)) || vb.metadata.length() != 0 ||
        va.glyphs.length() != vb.glyphs.length() ||
        std::memcmp(static_cast<const af::Glyph<REAL>*>(va.glyphs),
                    static_cast<const af::Glyph<REAL>*>(vb.glyphs),
                    va.glyphs.length() * sizeof(af::Glyph<REAL>)) != 0 ||
        va.kernPairs.length() != vb.kernPairs.length() ||
        std::memcmp(static_cast<const af::KernPair<REAL>*>(va.kernPairs),
                    static_cast<const af::KernPair<REAL>*>(vb.kernPairs),
                    va.kernPairs.length() * sizeof(af::KernPair<REAL>)) != 0)
        return false;
    const typename af::StdArteryFont<REAL>::Image& ia = a.images[0];
    const typename af::StdArteryFont<REAL>::Image& ib = b.images[0];
    if (ia.flags != ib.flags || ia.encoding != ib.encoding || ia.width != ib.width ||
        ia.height != ib.height || ia.channels != ib.channels ||
        ia.pixelFormat != ib.pixelFormat || ia.imageType != ib.imageType ||
        ia.rawBinaryFormat.rowLength != ib.rawBinaryFormat.rowLength ||
        ia.rawBinaryFormat.orientation != ib.rawBinaryFormat.orientation ||
        ia.childImages != ib.childImages || ia.textureFlags != ib.textureFlags ||
        !same(ia.metadata, static_cast<const char*>(ib.metadata)) ||
        ib.data.length() != 6 ||
        std::memcmp(static_cast<const unsigned char*>(ib.data), kPixels, 6) != 0)
        return false;
    return same(b.appendices[0].metadata, "notes") && b.appendices[0].data.length() == 5 &&
           std::memcmp(static_cast<const unsigned char*>(b.appendices[0].data), kAppendix, 5) == 0;
}

void test_crc32() {
    const unsigned char check_string[] = "123456789";
    check(crc32(check_string, 9) == 0xCBF43926u, "the reference CRC-32 of \"123456789\"");
    // The format stores the register without CRC-32's final inversion.
    uint32_t crc = af::crc32Init();
    for (int i = 0; i < 9; ++i)
        crc = af::crc32Update(crc, check_string[i]);
    check(crc == ~0xCBF43926u, "crc32Init and crc32Update");
}

void test_float_file() {
    af::StdArteryFont<float> font;
    fill(font);
    Bytes file;
    check(encode(font, file), "encode");

    // 112-byte header, 18 + 1 metadata bytes padded to 20, a 192-byte variant
    // header, the name's 11 bytes padded to 12, 2 * 48 glyph and 16 kerning
    // bytes, a 64-byte image header, "sdf" padded to 4, 6 pixels padded to 8,
    // an 8-byte appendix header, "notes" padded to 8, 5 bytes padded to 8,
    // and a 32-byte footer.
    const size_t size = 112 + 20 + (192 + 12 + 96 + 16) + (64 + 4 + 8) + (8 + 8 + 8) + 32;
    check(file.size() == size, "the encoded size");
    if (file.size() != size)
        return;
    check(std::memcmp(file.data(), "ARTERY/FONT\0\0\0\0\0", 16) == 0, "the header tag");
    check(le32(file, 16) == 0x4d276a5cu && le32(file, 20) == 1 && le32(file, 28) == 0x14,
          "the header magic number, version, and float real type");
    check(le32(file, 48) == af::METADATA_JSON && le32(file, 52) == 18 && le32(file, 56) == 1 &&
              le32(file, 60) == 316 && le32(file, 64) == 1 && le32(file, 68) == 76 &&
              le32(file, 72) == 1 && le32(file, 76) == 24,
          "the header's metadata, counts, and section lengths");
    check(le32(file, size - 28) == 0x55ccb363u && le32(file, size - 8) == size,
          "the footer magic number and total length");
    check(le32(file, size - 4) == ~crc32(file.data(), size - 4),
          "the footer checksum covers everything before it");

    af::StdArteryFont<float> decoded;
    check(decode(decoded, file) && same(font, decoded), "decode reproduces the font");

    af::StdArteryFont<double> wide;
    check(!decode(wide, file), "a float font does not decode as double");

    Bytes damaged = file;
    damaged[112 + 20 + 316 + 64 + 4] ^= 0x01;  // the first pixel
    check(!decode(decoded, damaged), "a damaged pixel fails the checksum");
    // With the checksum recomputed, the same bytes decode: only it refused them.
    const uint32_t crc = ~crc32(damaged.data(), size - 4);
    for (int i = 0; i < 4; ++i)
        damaged[size - 4 + i] = static_cast<unsigned char>(crc >> (8 * i));
    check(decode(decoded, damaged) &&
              static_cast<const unsigned char*>(decoded.images[0].data)[0] == (kPixels[0] ^ 1),
          "a damaged pixel under a matching checksum decodes");
    damaged = file;
    damaged[16] ^= 0x01;
    check(!decode(decoded, damaged), "a wrong magic number is refused");
    Bytes truncated(file.begin(), file.end() - 1);
    check(!decode(decoded, truncated), "a truncated file is refused");

    Bytes partial;
    check(!encode(font, partial, 100) && partial.size() == 100,
          "encode stops at a short write");
}

void test_double_file() {
    af::StdArteryFont<double> font;
    fill(font);
    Bytes file;
    check(encode(font, file), "encode with double");
    // The variant header and glyphs widen: 320 + 12 + 2 * 88 + 24 bytes.
    check(file.size() == 112 + 20 + (320 + 12 + 176 + 24) + 76 + 24 + 32 &&
              le32(file, 28) == 0x18 && le32(file, 60) == 532,
          "the double real type and variant length");
    af::StdArteryFont<double> decoded;
    check(decode(decoded, file) && same(font, decoded), "decode reproduces the double font");
}

void test_stdio() {
    // Among the probe's own build outputs: `cabin run` keeps the caller's
    // working directory, and MSVC's tmpfile() creates its file in the drive
    // root.
    const char* dir = std::getenv("CABIN_PROFILE_DIR");
    check(dir != NULL, "CABIN_PROFILE_DIR");
    if (dir == NULL)
        return;
    const std::string path = std::string(dir) + "/port-probe-artery-font.arfont";
    af::StdArteryFont<float> font;
    fill(font);
    check(af::writeFile(font, path.c_str()), "writeFile");
    af::StdArteryFont<float> decoded;
    check(af::readFile(decoded, path.c_str()) && same(font, decoded), "readFile");
    std::remove(path.c_str());
    check(!af::readFile(decoded, path.c_str()), "readFile of a missing file");
}

}  // namespace

int main() {
    test_crc32();
    test_float_file();
    test_double_file();
    test_stdio();
    if (failures != 0)
        return 1;
    std::printf("artery-font-format 1.1 ok\n");
    return 0;
}
