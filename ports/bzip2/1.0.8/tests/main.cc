// The C++98 consumer: round-trips data through the streaming API in
// small chunks, then writes a .bz2 stream to a scratch file and reads it
// back through BZ2_bzdopen.
#include <bzlib.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#ifdef _WIN32
#include <io.h>
#define dup _dup
#else
#include <unistd.h>
#endif

namespace {

int fail(const char* what) {
    std::fprintf(stderr, "bzip2 C++ test failed: %s\n", what);
    return 1;
}

std::string sample() {
    std::string text;
    for (int i = 0; i < 4000; ++i) {
        text += "Cabin consumes cabin-ports/bzip2 from C++98, line ";
        text += static_cast<char>('0' + i % 10);
        text += '\n';
    }
    return text;
}

// Feeds `text` to BZ2_bzCompress 1000 bytes at a time, then finishes.
bool compress_streaming(const std::string& text, std::vector<char>& stream) {
    bz_stream s;
    std::memset(&s, 0, sizeof s);
    if (BZ2_bzCompressInit(&s, 5, 0, 0) != BZ_OK) return false;
    std::vector<char> buf(4096);
    std::size_t pos = 0;
    int ret;
    do {
        const std::size_t chunk = text.size() - pos < 1000 ? text.size() - pos : 1000;
        s.next_in = const_cast<char*>(text.data()) + pos;
        s.avail_in = static_cast<unsigned int>(chunk);
        pos += chunk;
        const int action = pos == text.size() ? BZ_FINISH : BZ_RUN;
        do {
            s.next_out = &buf[0];
            s.avail_out = static_cast<unsigned int>(buf.size());
            ret = BZ2_bzCompress(&s, action);
            stream.insert(stream.end(), &buf[0], &buf[0] + (buf.size() - s.avail_out));
        } while (action == BZ_FINISH ? ret == BZ_FINISH_OK : ret == BZ_RUN_OK && s.avail_in != 0);
    } while (pos < text.size() && ret == BZ_RUN_OK);
    BZ2_bzCompressEnd(&s);
    return ret == BZ_STREAM_END;
}

bool decompress_streaming(const std::vector<char>& stream, std::string& text) {
    bz_stream s;
    std::memset(&s, 0, sizeof s);
    if (BZ2_bzDecompressInit(&s, 0, 0) != BZ_OK) return false;
    std::vector<char> buf(4096);
    s.next_in = const_cast<char*>(&stream[0]);
    s.avail_in = static_cast<unsigned int>(stream.size());
    int ret;
    // Stops once the input is used up, unless a full output buffer may
    // have left decoded data pending.
    do {
        s.next_out = &buf[0];
        s.avail_out = static_cast<unsigned int>(buf.size());
        ret = BZ2_bzDecompress(&s);
        text.append(&buf[0], buf.size() - s.avail_out);
    } while (ret == BZ_OK && (s.avail_in != 0 || s.avail_out == 0));
    BZ2_bzDecompressEnd(&s);
    return ret == BZ_STREAM_END;
}

bool round_trips_through_a_file(const std::string& text) {
    // Among the probe's own build outputs: `cabin run` keeps the caller's
    // working directory, and MSVC documents tmpfile() as creating its file
    // in the drive root.
    const char* const dir = std::getenv("CABIN_PROFILE_DIR");
    if (dir == 0) return false;
    const std::string path = std::string(dir) + "/port-probe-bzip2.tmp";
    std::FILE* file = std::fopen(path.c_str(), "w+b");
    if (file == 0) return false;
    int err;
    BZFILE* writer = BZ2_bzWriteOpen(&err, file, 9, 0, 0);
    if (err == BZ_OK) BZ2_bzWrite(&err, writer, const_cast<char*>(text.data()), static_cast<int>(text.size()));
    if (err == BZ_OK) BZ2_bzWriteClose(&err, writer, 0, 0, 0);
    std::rewind(file);

    // BZ2_bzclose closes the descriptor it was given, so hand it a copy.
    BZFILE* reader = err == BZ_OK ? BZ2_bzdopen(dup(fileno(file)), "rb") : 0;
    std::string read;
    if (reader != 0) {
        char buf[4096];
        int n;
        while ((n = BZ2_bzread(reader, buf, sizeof buf)) > 0) read.append(buf, n);
        BZ2_bzclose(reader);
    }
    std::fclose(file);
    std::remove(path.c_str());
    return read == text;
}

}  // namespace

int main() {
    const std::string text = sample();
    std::vector<char> stream;
    if (!compress_streaming(text, stream) || stream.size() >= text.size() / 8) {
        return fail("streaming compression");
    }
    std::string decoded;
    if (!decompress_streaming(stream, decoded) || decoded != text) return fail("streaming round trip");

    std::string partial;
    stream.resize(stream.size() - 8);
    if (decompress_streaming(stream, partial)) return fail("truncated stream accepted");

    if (!round_trips_through_a_file(text)) return fail("file round trip");
    return 0;
}
