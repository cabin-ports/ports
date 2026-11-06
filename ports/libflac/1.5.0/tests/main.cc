// The C++ consumer: libFLAC++'s Encoder::Stream and Decoder::Stream
// subclassed over memory, native FLAC and Ogg FLAC, its metadata classes,
// settings the encoder rejects, input that is not FLAC, and independent
// encoders and decoders on several threads at once.
#include <FLAC++/decoder.h>
#include <FLAC++/encoder.h>
#include <FLAC++/metadata.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace {

bool ok = true;

void check(bool pass, const char* what) {
    if (!pass) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ok = false;
    }
}

const uint32_t rate = 48000;
const uint32_t frames = 48000;

// 16-bit sawtooth per channel with pseudo-random noise.
std::vector<FLAC__int32> make_signal(uint32_t channels, uint32_t seed) {
    std::vector<FLAC__int32> pcm(frames * channels);
    uint32_t state = seed;
    for (uint32_t i = 0; i < frames; i++) {
        for (uint32_t c = 0; c < channels; c++) {
            const FLAC__int32 period = 64 + 29 * c + seed % 7;
            const FLAC__int32 ramp = FLAC__int32(i % period) * 512 / period - 256;
            state = state * 1664525u + 1013904223u;
            pcm[i * channels + c] = ramp * 40 + FLAC__int32(state >> 20) % 301 - 150;
        }
    }
    return pcm;
}

class MemoryEncoder : public FLAC::Encoder::Stream {
public:
    std::vector<FLAC__byte> data;

protected:
    ::FLAC__StreamEncoderReadStatus read_callback(FLAC__byte buffer[], size_t* bytes) override {
        const size_t n = std::min(*bytes, data.size() - pos);
        std::memcpy(buffer, data.data() + pos, n);
        pos += n;
        *bytes = n;
        return n ? FLAC__STREAM_ENCODER_READ_STATUS_CONTINUE : FLAC__STREAM_ENCODER_READ_STATUS_END_OF_STREAM;
    }
    ::FLAC__StreamEncoderWriteStatus write_callback(const FLAC__byte buffer[], size_t bytes, uint32_t,
                                                    uint32_t) override {
        if (pos + bytes > data.size()) data.resize(pos + bytes);
        std::memcpy(data.data() + pos, buffer, bytes);
        pos += bytes;
        return FLAC__STREAM_ENCODER_WRITE_STATUS_OK;
    }
    ::FLAC__StreamEncoderSeekStatus seek_callback(FLAC__uint64 offset) override {
        if (offset > data.size()) return FLAC__STREAM_ENCODER_SEEK_STATUS_ERROR;
        pos = size_t(offset);
        return FLAC__STREAM_ENCODER_SEEK_STATUS_OK;
    }
    ::FLAC__StreamEncoderTellStatus tell_callback(FLAC__uint64* offset) override {
        *offset = pos;
        return FLAC__STREAM_ENCODER_TELL_STATUS_OK;
    }

private:
    size_t pos = 0;
};

class MemoryDecoder : public FLAC::Decoder::Stream {
public:
    explicit MemoryDecoder(const std::vector<FLAC__byte>& in) : in(in) {}
    std::vector<FLAC__int32> pcm;
    FLAC::Metadata::StreamInfo info;
    FLAC::Metadata::VorbisComment tags;
    int errors = 0;

protected:
    ::FLAC__StreamDecoderReadStatus read_callback(FLAC__byte buffer[], size_t* bytes) override {
        const size_t n = std::min(*bytes, in.size() - pos);
        std::memcpy(buffer, in.data() + pos, n);
        pos += n;
        *bytes = n;
        return n ? FLAC__STREAM_DECODER_READ_STATUS_CONTINUE : FLAC__STREAM_DECODER_READ_STATUS_END_OF_STREAM;
    }
    ::FLAC__StreamDecoderSeekStatus seek_callback(FLAC__uint64 offset) override {
        if (offset > in.size()) return FLAC__STREAM_DECODER_SEEK_STATUS_ERROR;
        pos = size_t(offset);
        return FLAC__STREAM_DECODER_SEEK_STATUS_OK;
    }
    ::FLAC__StreamDecoderTellStatus tell_callback(FLAC__uint64* offset) override {
        *offset = pos;
        return FLAC__STREAM_DECODER_TELL_STATUS_OK;
    }
    ::FLAC__StreamDecoderLengthStatus length_callback(FLAC__uint64* length) override {
        *length = in.size();
        return FLAC__STREAM_DECODER_LENGTH_STATUS_OK;
    }
    bool eof_callback() override { return pos >= in.size(); }
    ::FLAC__StreamDecoderWriteStatus write_callback(const ::FLAC__Frame* frame,
                                                    const FLAC__int32* const buffer[]) override {
        for (uint32_t i = 0; i < frame->header.blocksize; i++) {
            for (uint32_t c = 0; c < frame->header.channels; c++) pcm.push_back(buffer[c][i]);
        }
        return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
    }
    void metadata_callback(const ::FLAC__StreamMetadata* metadata) override {
        if (metadata->type == FLAC__METADATA_TYPE_STREAMINFO) info = metadata;
        if (metadata->type == FLAC__METADATA_TYPE_VORBIS_COMMENT) tags = metadata;
    }
    void error_callback(::FLAC__StreamDecoderErrorStatus) override { errors++; }

private:
    const std::vector<FLAC__byte>& in;
    size_t pos = 0;
};

std::vector<FLAC__byte> encode(const std::vector<FLAC__int32>& pcm, uint32_t channels, bool ogg,
                               FLAC::Metadata::Prototype** metadata = nullptr, uint32_t blocks = 0) {
    MemoryEncoder encoder;
    bool good = encoder.is_valid() && encoder.set_channels(channels) && encoder.set_bits_per_sample(16) &&
                encoder.set_sample_rate(rate) && encoder.set_compression_level(6) && encoder.set_verify(true) &&
                encoder.set_total_samples_estimate(frames) && (!ogg || encoder.set_ogg_serial_number(7)) &&
                (blocks == 0 || encoder.set_metadata(metadata, blocks));
    good = good && (ogg ? encoder.init_ogg() : encoder.init()) == FLAC__STREAM_ENCODER_INIT_STATUS_OK;
    good = good && encoder.process_interleaved(pcm.data(), frames);
    good = encoder.finish() && good;
    if (!good) encoder.data.clear();
    return encoder.data;
}

// Decodes with MD5 checking; true when the stream ends cleanly and its
// signature matches.
bool decode(MemoryDecoder& decoder, bool ogg) {
    decoder.set_md5_checking(true);
    decoder.set_metadata_respond_all();
    bool good = (ogg ? decoder.init_ogg() : decoder.init()) == FLAC__STREAM_DECODER_INIT_STATUS_OK &&
                decoder.process_until_end_of_stream() &&
                ::FLAC__StreamDecoderState(decoder.get_state()) == FLAC__STREAM_DECODER_END_OF_STREAM;
    return decoder.finish() && good;
}

bool round_trips(uint32_t channels, uint32_t seed, bool ogg) {
    const std::vector<FLAC__int32> pcm = make_signal(channels, seed);
    const std::vector<FLAC__byte> stream = encode(pcm, channels, ogg);
    MemoryDecoder decoder(stream);
    return stream.size() > 4 && std::memcmp(stream.data(), ogg ? "OggS" : "fLaC", 4) == 0 &&
           stream.size() < pcm.size() * 2 && decode(decoder, ogg) && decoder.errors == 0 && decoder.pcm == pcm &&
           decoder.info.get_total_samples() == frames && decoder.info.get_channels() == channels;
}

void with_metadata() {
    const std::vector<FLAC__int32> pcm = make_signal(2, 1);
    FLAC::Metadata::VorbisComment tags;
    const FLAC::Metadata::VorbisComment::Entry title("TITLE", "Sawtooth");
    check(title.is_valid() && std::strcmp(title.get_field_name(), "TITLE") == 0 &&
              std::strcmp(title.get_field_value(), "Sawtooth") == 0,
          "VorbisComment::Entry");
    check(tags.append_comment(title) && tags.append_comment(FLAC::Metadata::VorbisComment::Entry("ARTIST", "Cabin")),
          "VorbisComment::append_comment");
    FLAC::Metadata::Padding padding(512);
    FLAC::Metadata::Prototype* blocks[] = {&tags, &padding};

    const std::vector<FLAC__byte> stream = encode(pcm, 2, false, blocks, 2);
    MemoryDecoder decoder(stream);
    check(decode(decoder, false) && decoder.pcm == pcm, "native FLAC with metadata round-trips");
    check(decoder.tags.is_valid() && decoder.tags.get_num_comments() == 2 &&
              std::string(decoder.tags.get_comment(0).get_field_value()) == "Sawtooth" &&
              std::string(decoder.tags.get_comment(1).get_field_name()) == "ARTIST",
          "the Vorbis comment survives");
    check(std::strncmp(reinterpret_cast<const char*>(decoder.tags.get_vendor_string()), "reference libFLAC 1.5.0",
                       23) == 0,
          "the encoder signs the vendor string");
    static const FLAC__byte zero[16] = {0};
    check(std::memcmp(decoder.info.get_md5sum(), zero, 16) != 0, "STREAMINFO carries the MD5 signature");
}

void rejected_and_foreign_input() {
    MemoryEncoder encoder;
    check(encoder.set_bits_per_sample(3) && encoder.init() == FLAC__STREAM_ENCODER_INIT_STATUS_INVALID_BITS_PER_SAMPLE,
          "3-bit samples are rejected");

    const std::string text(4096, 'x');
    const std::vector<FLAC__byte> garbage(text.begin(), text.end());
    MemoryDecoder decoder(garbage);
    decode(decoder, false);
    check(decoder.errors > 0 && decoder.pcm.empty(), "input that is not FLAC is reported, not decoded");
}

// Encoders and decoders are independent objects, used on four threads at
// once.
void concurrent() {
    bool good[4] = {false, false, false, false};
    std::vector<std::thread> workers;
    for (uint32_t t = 0; t < 4; t++) {
        workers.emplace_back([&good, t] { good[t] = round_trips(1 + t, 100 + t, t % 2 == 1); });
    }
    for (std::thread& worker : workers) worker.join();
    for (bool g : good) check(g, "concurrent encoding and decoding");
}

}  // namespace

int main() {
    check(round_trips(2, 7, false), "native FLAC round-trips");
    check(round_trips(6, 9, true), "six-channel Ogg FLAC round-trips");
    with_metadata();
    rejected_and_foreign_input();
    concurrent();
    std::printf("libflac (C++): %s\n", ok ? "ok" : "FAILED");
    return ok ? 0 : 1;
}
