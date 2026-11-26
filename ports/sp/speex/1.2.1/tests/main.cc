// The C++ consumer: a narrowband round trip of a 440 Hz tone at quality 4
// through the float API (frame size, bitrate, exact bits per frame,
// decoded energy and pitch, and concealment of a lost frame), the
// wideband mode's queries and the stream header it describes.
#include <speex/speex.h>
#include <speex/speex_header.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

const double kPi = 3.14159265358979323846;
const int kRate = 8000;
const int kFrame = 160;
const int kFrames = 50;
const double kTone = 440.0;

bool fail(const char* what) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    return false;
}

double energy_of(const float* x, int n) {
    double energy = 0.0;
    for (int i = 0; i < n; i++) {
        energy += static_cast<double>(x[i]) * x[i];
    }
    return energy;
}

// The share of x[0..n)'s energy at kTone: 1 for a pure tone on a bin.
double tone_share(const float* x, int n) {
    double re = 0.0, im = 0.0;
    for (int i = 0; i < n; i++) {
        re += x[i] * std::cos(2.0 * kPi * kTone * i / kRate);
        im += x[i] * std::sin(2.0 * kPi * kTone * i / kRate);
    }
    return 2.0 * (re * re + im * im) / (n * energy_of(x, n) + 1.0);
}

bool describes_wideband() {
    bool ok = true;
    int n = 0;
    speex_mode_query(&speex_wb_mode, SPEEX_MODE_FRAME_SIZE, &n);
    if (n != 320) {
        ok = fail("a wideband frame is 320 samples");
    }
    n = 0;
    speex_mode_query(&speex_wb_mode, SPEEX_SUBMODE_BITS_PER_FRAME, &n);
    if (n != 4) {
        ok = fail("the wideband null submode is 4 bits");
    }
    if (std::strcmp(speex_wb_mode.modeName, "wideband (sub-band CELP)") != 0) {
        ok = fail("the wideband mode's name");
    }

    SpeexHeader header;
    speex_init_header(&header, 16000, 2, &speex_wb_mode);
    if (std::memcmp(header.speex_string, "Speex   ", 8) != 0 ||
        std::strcmp(header.speex_version, "1.2.1") != 0 || header.speex_version_id != 1 ||
        header.header_size != static_cast<spx_int32_t>(sizeof(SpeexHeader)) ||
        header.rate != 16000 || header.mode != 1 || header.mode_bitstream_version != 4 ||
        header.nb_channels != 2 || header.bitrate != -1 || header.frame_size != 320 ||
        header.vbr != 0 || header.frames_per_packet != 0 || header.extra_headers != 0 ||
        header.reserved1 != 0 || header.reserved2 != 0) {
        ok = fail("speex_init_header's fields for wideband stereo");
    }
    return ok;
}

bool narrowband_round_trip() {
    void* enc = speex_encoder_init(&speex_nb_mode);
    void* dec = speex_decoder_init(&speex_nb_mode);
    if (enc == NULL || dec == NULL) {
        return fail("init");
    }
    std::vector<float> input(kFrames * kFrame), output((kFrames + 1) * kFrame);
    for (int i = 0; i < kFrames * kFrame; i++) {
        input[i] = static_cast<float>(8000.0 * std::sin(2.0 * kPi * kTone * i / kRate));
    }
    bool ok = true;
    spx_int32_t quality = 4, value = 0;
    speex_encoder_ctl(enc, SPEEX_GET_FRAME_SIZE, &value);
    if (value != kFrame) {
        ok = fail("the encoder takes 160-sample frames");
    }
    speex_encoder_ctl(enc, SPEEX_SET_QUALITY, &quality);
    speex_encoder_ctl(enc, SPEEX_GET_MODE, &value);
    if (value != 3) {
        ok = fail("quality 4 selects submode 3");
    }
    speex_encoder_ctl(enc, SPEEX_GET_BITRATE, &value);
    if (value != 8000) {
        ok = fail("submode 3 is 8 kb/s");
    }
    speex_decoder_ctl(dec, SPEEX_GET_ENH, &value);
    if (value != 1) {
        ok = fail("the decoder's enhancer is on by default");
    }

    SpeexBits bits;
    speex_bits_init(&bits);
    char packet[200];
    for (int frame = 0; frame < kFrames; frame++) {
        speex_bits_reset(&bits);
        if (speex_encode(enc, &input[frame * kFrame], &bits) != 1) {
            return fail("speex_encode");
        }
        // 160 bits fill 20 bytes, so no terminator follows them.
        if (bits.nbBits != 160 || speex_bits_nbytes(&bits) != 20 ||
            speex_bits_write(&bits, packet, static_cast<int>(sizeof packet)) != 20) {
            ok = fail("a submode 3 frame is 160 bits in 20 bytes");
        }
        speex_bits_read_from(&bits, packet, 20);
        if (speex_decode(dec, &bits, &output[frame * kFrame]) != 0) {
            return fail("speex_decode");
        }
        if (speex_bits_remaining(&bits) != 0) {
            ok = fail("the decoder reads the whole frame");
        }
    }
    // Frame kFrames is lost: the decoder conceals it.
    if (speex_decode(dec, NULL, &output[kFrames * kFrame]) != 0) {
        return fail("concealment");
    }

    const float* tail = &output[kFrames / 2 * kFrame];
    const int span = kFrames / 2 * kFrame;
    double ratio = energy_of(tail, span) / energy_of(&input[kFrames / 2 * kFrame], span);
    double share = tone_share(tail, span);
    double concealed = energy_of(&output[kFrames * kFrame], kFrame) / energy_of(tail, kFrame);
    std::printf("narrowband: energy ratio %.3f, tone share %.3f, concealed %.3f\n", ratio, share,
                concealed);
    // Negated ranges, so that a NaN sample fails them.
    if (!(ratio >= 0.5 && ratio <= 2.0)) {
        ok = fail("the decoded energy is within a factor 2 of the input's");
    }
    if (!(share >= 0.9)) {
        ok = fail("the decoded signal is the 440 Hz tone");
    }
    if (!(concealed >= 0.1 && concealed <= 2.0)) {
        ok = fail("the concealed frame continues the tone");
    }
    speex_bits_destroy(&bits);
    speex_encoder_destroy(enc);
    speex_decoder_destroy(dec);
    return ok;
}

}  // namespace

int main() {
    bool ok = describes_wideband();
    ok = narrowband_round_trip() && ok;
    return ok ? 0 : 1;
}
