// Encodes a stereo ramp at 44.1 kHz as MPEG-4 and as MPEG-2 AAC-LC ADTS and
// checks every frame header against ISO/IEC 13818-7 6.2 / 14496-3 1.A.2.2.
#include <faac.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

const std::uint32_t kRate = 44100;
const std::uint32_t kChannels = 2;
const std::uint32_t kFrames = 6;

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

// ID is 0 for MPEG-4 and 1 for MPEG-2; AOT 2 (LC) is profile 1; 44100 Hz is
// index 4; stereo is channel configuration 2.
void check_adts(const std::uint8_t* f, std::uint32_t size, unsigned id) {
    check(size >= 7, "ADTS frame holds its header");
    check(f[0] == 0xFF && (f[1] & 0xF0) == 0xF0, "ADTS syncword 0xFFF");
    check(((f[1] >> 3) & 1) == id, "ADTS ID");
    check((f[1] & 0x06) == 0, "ADTS layer 0");
    check(f[2] >> 6 == 1, "ADTS profile LC");
    check(((f[2] >> 2) & 0x0F) == 4, "ADTS sampling frequency index 4");
    check((((f[2] & 1) << 2) | (f[3] >> 6)) == 2, "ADTS channel configuration 2");
    check(((static_cast<std::uint32_t>(f[3] & 3) << 11) | (static_cast<std::uint32_t>(f[4]) << 3) |
           (f[5] >> 5)) == size,
          "ADTS frame length is the bytes written");
}

void run(faac_stream_format format, unsigned id) {
    faac_params p;
    check(faac_params_init(&p, sizeof p) == FAAC_OK, "faac_params_init");
    p.sample_rate = kRate;
    p.channels = kChannels;
    p.object_type = FAAC_OBJ_LC;
    p.bit_rate = 96000;
    p.stream_format = format;
    p.stereo_mode = FAAC_STEREO_MS;
    p.flags = FAAC_FLAG_TNS;

    faac_encoder* enc = nullptr;
    check(faac_encoder_open(&p, &enc) == FAAC_OK && enc != nullptr, "faac_encoder_open");
    if (enc == nullptr) {
        return;
    }
    faac_encoder_info info = {};
    info.struct_size = sizeof info;
    check(faac_encoder_get_info(enc, &info) == FAAC_OK, "faac_encoder_get_info");
    check(info.frame_samples == 1024, "LC frame of 1024 samples");
    check(info.object_type == FAAC_OBJ_LC, "object type LC");
    check(info.stream_format == format, "stream format");
    check(info.bit_rate == 96000, "bit rate 96000");

    const std::uint8_t* asc = nullptr;
    std::uint32_t asc_size = 0;
    faac_status st = faac_encoder_asc(enc, &asc, &asc_size);
    if (id == 1) {
        check(st == FAAC_ERR_UNSUPPORTED && info.asc_size == 0, "MPEG-2 has no AudioSpecificConfig");
    } else {
        // AOT 2, frequency index 4, channel configuration 2, then three zero
        // GASpecificConfig flags.
        check(st == FAAC_OK && asc_size == 2 && asc[0] == 0x12 && asc[1] == 0x10,
              "LC AudioSpecificConfig");
    }

    std::vector<std::int16_t> pcm(info.frame_samples * kChannels);
    std::vector<std::uint8_t> out(info.max_output_bytes);
    std::uint32_t frames = 0;
    bool flushed = false;
    for (std::uint32_t i = 0; i < kFrames + 16 && !flushed; ++i) {
        std::uint32_t n = i < kFrames ? info.frame_samples : 0;
        // A 344.5 Hz sawtooth on the left, its reverse on the right.
        for (std::uint32_t s = 0; s < n; ++s) {
            std::int16_t v = static_cast<std::int16_t>(static_cast<int>((i * n + s) % 128) * 100 - 6400);
            pcm[2 * s] = v;
            pcm[2 * s + 1] = static_cast<std::int16_t>(-v);
        }
        std::uint32_t written = 0;
        if (faac_encoder_encode(enc, pcm.data(), n * kChannels, out.data(),
                                static_cast<std::uint32_t>(out.size()), &written) != FAAC_OK) {
            check(false, "faac_encoder_encode");
            break;
        }
        if (written > 0) {
            check_adts(out.data(), written, id);
            ++frames;
        } else if (i >= kFrames) {
            flushed = true;
        }
    }
    check(flushed, "flush ends with an empty call");
    check(frames * info.frame_samples >= kFrames * info.frame_samples + info.encoder_delay,
          "frames cover the input and the priming delay");
    check(faac_encoder_close(&enc) == FAAC_OK && enc == nullptr, "faac_encoder_close");
}

}  // namespace

int main() {
    faac_library_info lib = {};
    lib.struct_size = sizeof lib;
    check(faac_get_library_info(&lib) == FAAC_OK, "faac_get_library_info");
    check(FAAC_VERSION_HEX == 0x030001 && lib.version != nullptr &&
              std::strcmp(lib.version, "3.0.1") == 0,
          "version 3.0.1");

    run(FAAC_STREAM_ADTS, 0);
    run(FAAC_STREAM_ADTS_MPEG2, 1);

    if (failures != 0) {
        return 1;
    }
    std::puts("faac C++ consumer: ok");
    return 0;
}
