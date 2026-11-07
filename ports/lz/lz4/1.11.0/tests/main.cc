// Decodes a fixed LZ4 block, round-trips data through the block, HC,
// and frame APIs, and checks the frame's content checksum, which runs
// through lz4's bundled xxHash.
#include <lz4.h>
#include <lz4file.h>
#include <lz4frame.h>
#include <lz4hc.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// The bundled xxHash is compiled with XXH_NAMESPACE=LZ4_; without the
// rename this would not link.
extern "C" unsigned LZ4_XXH_versionNumber(void);

static bool fail(const char* what) {
    std::fprintf(stderr, "lz4 test failed: %s\n", what);
    return false;
}

static bool decodes_a_known_block() {
    // Two sequences: 5 literals "abcde" and a 10-byte match at offset 5
    // (match length nibble 6 + the 4-byte minimum), then the mandatory
    // literals-only final sequence "vwxyz".
    const unsigned char block[] = {0x56, 'a', 'b', 'c', 'd', 'e', 0x05, 0x00,
                                   0x50, 'v', 'w', 'x', 'y', 'z'};
    const char expected[] = "abcdeabcdeabcdevwxyz";
    char out[sizeof expected - 1];
    const int n = LZ4_decompress_safe(reinterpret_cast<const char*>(block), out, sizeof block,
                                      sizeof out);
    return (n == static_cast<int>(sizeof out) && std::memcmp(out, expected, sizeof out) == 0) ||
           fail("known block");
}

static std::string sample() {
    std::string s;
    for (int i = 0; i < 400; ++i) {
        s += "Cabin consumes cabin-ports/lz4 from the registry; line ";
        s += std::to_string(i % 7);
        s += '\n';
    }
    return s;
}

static bool round_trips_blocks(const std::string& in) {
    const int bound = LZ4_compressBound(static_cast<int>(in.size()));
    std::vector<char> packed(bound);
    std::vector<char> out(in.size());
    const int fast = LZ4_compress_default(in.data(), packed.data(), static_cast<int>(in.size()), bound);
    if (fast <= 0 || fast >= static_cast<int>(in.size()) / 4) return fail("compress_default");
    if (LZ4_decompress_safe(packed.data(), out.data(), fast, static_cast<int>(out.size())) !=
            static_cast<int>(in.size()) ||
        std::memcmp(out.data(), in.data(), in.size()) != 0) {
        return fail("block round trip");
    }
    const int hc = LZ4_compress_HC(in.data(), packed.data(), static_cast<int>(in.size()), bound,
                                   LZ4HC_CLEVEL_MAX);
    if (hc <= 0 || hc > fast) return fail("compress_HC");
    if (LZ4_decompress_safe(packed.data(), out.data(), hc, static_cast<int>(out.size())) !=
            static_cast<int>(in.size()) ||
        std::memcmp(out.data(), in.data(), in.size()) != 0) {
        return fail("HC round trip");
    }
    // A corrupted stream must be rejected, not decoded.
    packed[0] = '\xF0';
    return LZ4_decompress_safe(packed.data(), out.data(), 3, static_cast<int>(out.size())) < 0 ||
           fail("corrupt block accepted");
}

static bool round_trips_a_checksummed_frame(const std::string& in) {
    LZ4F_preferences_t prefs;
    std::memset(&prefs, 0, sizeof prefs);
    prefs.frameInfo.contentChecksumFlag = LZ4F_contentChecksumEnabled;
    prefs.frameInfo.contentSize = in.size();
    std::vector<unsigned char> frame(LZ4F_compressFrameBound(in.size(), &prefs));
    const size_t n = LZ4F_compressFrame(frame.data(), frame.size(), in.data(), in.size(), &prefs);
    if (LZ4F_isError(n)) return fail(LZ4F_getErrorName(n));
    // LZ4 frame magic number 0x184D2204, little-endian.
    const unsigned char magic[] = {0x04, 0x22, 0x4D, 0x18};
    if (n < sizeof magic || std::memcmp(frame.data(), magic, sizeof magic) != 0) return fail("magic");

    LZ4F_dctx* dctx = nullptr;
    if (LZ4F_isError(LZ4F_createDecompressionContext(&dctx, LZ4F_VERSION))) return fail("dctx");
    std::vector<char> out(in.size());
    size_t src = n;
    size_t dst = out.size();
    const size_t left = LZ4F_decompress(dctx, out.data(), &dst, frame.data(), &src, nullptr);
    bool ok = !LZ4F_isError(left) && left == 0 && dst == in.size() &&
              std::memcmp(out.data(), in.data(), in.size()) == 0;
    if (!ok) fail("frame round trip");

    // Flip one byte of the stored content checksum: decoding must fail.
    frame[n - 1] ^= 0x01;
    LZ4F_resetDecompressionContext(dctx);
    src = n;
    dst = out.size();
    if (!LZ4F_isError(LZ4F_decompress(dctx, out.data(), &dst, frame.data(), &src, nullptr))) {
        ok = fail("bad checksum accepted");
    }
    LZ4F_freeDecompressionContext(dctx);
    return ok;
}

int main() {
    const std::string in = sample();
    const bool ok = (LZ4_versionNumber() == LZ4_VERSION_NUMBER || fail("version")) &&
                    (LZ4_XXH_versionNumber() / 100 == 6 || fail("bundled xxHash")) &&
                    decodes_a_known_block() && round_trips_blocks(in) &&
                    round_trips_a_checksummed_frame(in);
    return ok ? 0 : 1;
}
