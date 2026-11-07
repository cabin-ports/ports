// The C++11 consumer: round-trips data through the streaming API,
// rejects a truncated frame, and checks the library version.
#include <zstd.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

int fail(const char* what) {
    std::fprintf(stderr, "zstd C++ test failed: %s\n", what);
    return 1;
}

std::string sample() {
    std::string text;
    for (int i = 0; i < 5000; ++i) {
        text += "Cabin consumes cabin-ports/zstd from C++11, line " + std::to_string(i % 97) + "\n";
    }
    return text;
}

// Feeds `text` to ZSTD_compressStream2 in small chunks, then ends the frame.
std::vector<char> compress_streaming(const std::string& text) {
    ZSTD_CCtx* cctx = ZSTD_createCCtx();
    ZSTD_CCtx_setParameter(cctx, ZSTD_c_compressionLevel, 19);
    std::vector<char> frame;
    std::vector<char> buf(ZSTD_CStreamOutSize());
    const std::size_t chunk = 1000;
    for (std::size_t pos = 0; pos < text.size(); pos += chunk) {
        const bool last = pos + chunk >= text.size();
        ZSTD_inBuffer input = {text.data() + pos, last ? text.size() - pos : chunk, 0};
        const ZSTD_EndDirective mode = last ? ZSTD_e_end : ZSTD_e_continue;
        std::size_t remaining;
        do {
            ZSTD_outBuffer output = {buf.data(), buf.size(), 0};
            remaining = ZSTD_compressStream2(cctx, &output, &input, mode);
            if (ZSTD_isError(remaining)) {
                ZSTD_freeCCtx(cctx);
                return {};
            }
            frame.insert(frame.end(), buf.data(), buf.data() + output.pos);
        } while (last ? remaining != 0 : input.pos != input.size);
    }
    ZSTD_freeCCtx(cctx);
    return frame;
}

// Returns false if the stream reports an error or ends mid-frame.
bool decompress_streaming(const std::vector<char>& frame, std::string& text) {
    ZSTD_DCtx* dctx = ZSTD_createDCtx();
    std::vector<char> buf(ZSTD_DStreamOutSize());
    ZSTD_inBuffer input = {frame.data(), frame.size(), 0};
    std::size_t hint = 1;
    while (input.pos < input.size) {
        ZSTD_outBuffer output = {buf.data(), buf.size(), 0};
        hint = ZSTD_decompressStream(dctx, &output, &input);
        if (ZSTD_isError(hint)) break;
        text.append(buf.data(), output.pos);
    }
    ZSTD_freeDCtx(dctx);
    return !ZSTD_isError(hint) && hint == 0;
}

}  // namespace

int main() {
    const std::string text = sample();
    const std::vector<char> frame = compress_streaming(text);
    if (frame.empty() || frame.size() >= text.size() / 8) return fail("streaming compression");

    std::string decoded;
    if (!decompress_streaming(frame, decoded) || decoded != text) return fail("streaming round trip");

    std::string partial;
    const std::vector<char> truncated(frame.begin(), frame.end() - 4);
    if (decompress_streaming(truncated, partial)) return fail("truncated frame accepted");

    if (std::strcmp(ZSTD_versionString(), ZSTD_VERSION_STRING) != 0) return fail("version");
    return 0;
}
