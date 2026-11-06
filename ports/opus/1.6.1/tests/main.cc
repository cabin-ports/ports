// The C++ consumer, in C++98 like the headers: SILK round trips of a
// synthetic vowel (fidelity, the decoder's pitch, voice-activity-driven
// DTX, and in-band FEC recovery of a lost frame), repacketizing and
// padding, and a first-order ambisonics round trip through the projection
// API.
#include <opus.h>
#include <opus_projection.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {

const double kPi = 3.14159265358979323846;
const int kRate = 16000;
const int kFrame = 320;  // 20 ms
const int kVoiced = 50;  // frames of vowel, then as many of faint noise
const int kPeriod = 128;  // a 125 Hz voice
const int kMaxPacket = 1500;

bool fail(const char* what) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    return false;
}

// An impulse train at kPeriod through three formant resonators (an "a"),
// swelling four times a second like syllables, over a faint noise floor;
// then the noise floor alone.  (SILK's voice activity detector learns a
// steady sound as background noise.)
std::vector<opus_int16> vowel_then_noise() {
    const double formants[3][2] = {{700, 130}, {1220, 70}, {2600, 160}};
    std::vector<double> x(2 * kVoiced * kFrame, 0.0);
    unsigned seed = 1;
    for (int n = 0; n < kVoiced * kFrame; n += kPeriod) {
        x[n] = 0.55 - 0.45 * std::cos(2.0 * kPi * 4.0 * n / kRate);
    }
    for (int f = 0; f < 3; f++) {
        double r = std::exp(-kPi * formants[f][1] / kRate);
        double c = 2.0 * r * std::cos(2.0 * kPi * formants[f][0] / kRate);
        double y1 = 0.0, y2 = 0.0;
        for (size_t n = 0; n < x.size(); n++) {
            double y = x[n] + c * y1 - r * r * y2;
            y2 = y1;
            y1 = y;
            x[n] = y;
        }
    }
    double peak = 0.0;
    for (size_t n = 0; n < x.size(); n++) {
        peak = std::max(peak, std::fabs(x[n]));
    }
    std::vector<opus_int16> pcm(x.size());
    for (size_t n = 0; n < x.size(); n++) {
        seed = seed * 1103515245u + 12345u;
        double noise = static_cast<double>((seed >> 16) & 0x1f) - 15.5;
        pcm[n] = static_cast<opus_int16>(12000.0 * x[n] / peak + noise);
    }
    return pcm;
}

double snr_db(const std::vector<opus_int16>& ref, const std::vector<opus_int16>& out, long from,
              long to, long lag) {
    double signal = 0.0, noise = 0.0;
    for (long i = from; i < to; i++) {
        double r = ref[i - lag];
        double d = r - out[i];
        signal += r * r;
        noise += d * d;
    }
    return 10.0 * std::log10(signal / (noise + 1.0));
}

double correlation(const opus_int16* a, const opus_int16* b, int n) {
    double ab = 0.0, aa = 0.0, bb = 0.0;
    for (int i = 0; i < n; i++) {
        ab += static_cast<double>(a[i]) * b[i];
        aa += static_cast<double>(a[i]) * a[i];
        bb += static_cast<double>(b[i]) * b[i];
    }
    return ab / std::sqrt(aa * bb + 1.0);
}

struct Encoded {
    std::vector<std::vector<unsigned char> > packets;
    int lookahead;
};

bool encode(const std::vector<opus_int16>& pcm, int frames, int bitrate, bool dtx, bool fec,
            Encoded* out) {
    int error = 0;
    OpusEncoder* enc = opus_encoder_create(kRate, 1, OPUS_APPLICATION_VOIP, &error);
    if (enc == NULL || error != OPUS_OK) {
        return fail("encoder");
    }
    opus_encoder_ctl(enc, OPUS_SET_BITRATE(bitrate));
    opus_encoder_ctl(enc, OPUS_SET_COMPLEXITY(10));
    opus_encoder_ctl(enc, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
    opus_encoder_ctl(enc, OPUS_SET_DTX(dtx ? 1 : 0));
    opus_encoder_ctl(enc, OPUS_SET_INBAND_FEC(fec ? 1 : 0));
    opus_encoder_ctl(enc, OPUS_SET_PACKET_LOSS_PERC(fec ? 20 : 0));
    opus_encoder_ctl(enc, OPUS_GET_LOOKAHEAD(&out->lookahead));
    bool ok = true;
    unsigned char packet[kMaxPacket];
    for (int f = 0; f < frames; f++) {
        int len = opus_encode(enc, &pcm[f * kFrame], kFrame, packet, kMaxPacket);
        if (len < 1) {
            opus_encoder_destroy(enc);
            return fail("opus_encode");
        }
        out->packets.push_back(std::vector<unsigned char>(packet, packet + len));
    }
    int in_dtx = 0;
    opus_encoder_ctl(enc, OPUS_GET_IN_DTX(&in_dtx));
    if (dtx && !in_dtx) {
        ok = fail("the encoder is in DTX after a second of noise floor");
    }
    opus_encoder_destroy(enc);
    return ok;
}

bool decode(OpusDecoder* dec, const std::vector<unsigned char>& packet, opus_int16* pcm) {
    return opus_decode(dec, &packet[0], static_cast<opus_int32>(packet.size()), pcm, kFrame, 0) ==
           kFrame;
}

bool silk_round_trip(const std::vector<opus_int16>& pcm, long* delay) {
    Encoded encoded;
    if (!encode(pcm, kVoiced, 20000, false, false, &encoded)) {
        return false;
    }
    int error = 0;
    OpusDecoder* dec = opus_decoder_create(kRate, 1, &error);
    if (dec == NULL) {
        return fail("decoder");
    }
    bool ok = true;
    int voiced = 0;
    std::vector<opus_int16> out(pcm.size());
    for (int f = 0; f < kVoiced; f++) {
        const std::vector<unsigned char>& packet = encoded.packets[f];
        // Configurations 8-11 are wideband SILK of 10, 20, 40 and 60 ms.
        if ((packet[0] >> 3) != 9 || opus_packet_get_bandwidth(&packet[0]) != OPUS_BANDWIDTH_WIDEBAND) {
            ok = fail("a wideband SILK packet of one 20 ms frame");
        }
        if (!decode(dec, packet, &out[f * kFrame])) {
            return fail("opus_decode");
        }
        // The pitch lag of a voiced frame, at 48 kHz; 0 for an unvoiced one
        // (the quiet ends of each syllable).  Within two 16 kHz lags of the
        // vowel's period, far from its octaves.
        opus_int32 pitch = 0;
        opus_decoder_ctl(dec, OPUS_GET_PITCH(&pitch));
        if (pitch != 0) {
            voiced++;
            if (pitch < 3 * (kPeriod - 2) || pitch > 3 * (kPeriod + 2)) {
                ok = fail("a voiced frame's pitch is the vowel's");
            }
        }
    }
    double best = -100.0;
    for (long lag = 0; lag <= 4 * encoded.lookahead; lag++) {
        double snr = snr_db(pcm, out, kVoiced / 2 * kFrame, kVoiced * kFrame, lag);
        if (snr > best) {
            best = snr;
            *delay = lag;
        }
    }
    std::printf("silk: delay %ld, snr %.1f dB, %d of %d frames voiced\n", *delay, best, voiced,
                kVoiced);
    if (*delay != encoded.lookahead) {
        ok = fail("the output lags the input by the lookahead");
    }
    if (best < 8.0) {
        ok = fail("20 kb/s SILK keeps the vowel above 8 dB SNR");
    }
    if (voiced < kVoiced / 2) {
        ok = fail("most of the vowel decodes as voiced");
    }
    opus_decoder_destroy(dec);
    return ok;
}

// The encoder stops sending (packets of at most two bytes) only once the
// vowel gives way to the noise floor, and the decoder fills the gap.
bool dtx_follows_voice_activity(const std::vector<opus_int16>& pcm) {
    Encoded encoded;
    if (!encode(pcm, 2 * kVoiced, 20000, true, false, &encoded)) {
        return false;
    }
    int error = 0;
    OpusDecoder* dec = opus_decoder_create(kRate, 1, &error);
    if (dec == NULL) {
        return fail("decoder");
    }
    int voiced_dtx = 0, noise_dtx = 0;
    std::vector<opus_int16> out(kFrame);
    for (int f = 0; f < 2 * kVoiced; f++) {
        if (encoded.packets[f].size() <= 2) {
            (f < kVoiced ? voiced_dtx : noise_dtx)++;
        }
        if (!decode(dec, encoded.packets[f], &out[0])) {
            return fail("decoding through DTX");
        }
    }
    std::printf("dtx: %d of %d voiced and %d of %d noise frames\n", voiced_dtx, kVoiced, noise_dtx,
                kVoiced);
    opus_decoder_destroy(dec);
    bool ok = true;
    if (voiced_dtx != 0) {
        ok = fail("no DTX while the vowel sounds");
    }
    if (noise_dtx < kVoiced / 5) {
        ok = fail("DTX over the noise floor");
    }
    return ok;
}

// Frame `lost` never arrives: the next packet's low-bitrate redundancy
// (LBRR) brings it back.
bool fec_recovers_a_lost_frame(const std::vector<opus_int16>& pcm, long delay) {
    Encoded encoded;
    if (!encode(pcm, kVoiced, 32000, false, true, &encoded)) {
        return false;
    }
    const int lost = 30;
    const std::vector<unsigned char>& next = encoded.packets[lost + 1];
    if (opus_packet_has_lbrr(&next[0], static_cast<opus_int32>(next.size())) != 1) {
        return fail("a packet carries redundancy for the one before");
    }
    int error = 0;
    OpusDecoder* dec = opus_decoder_create(kRate, 1, &error);
    if (dec == NULL) {
        return fail("decoder");
    }
    std::vector<opus_int16> out(kVoiced * kFrame);
    for (int f = 0; f < lost; f++) {
        if (!decode(dec, encoded.packets[f], &out[f * kFrame])) {
            return fail("opus_decode");
        }
    }
    if (opus_decode(dec, &next[0], static_cast<opus_int32>(next.size()), &out[lost * kFrame],
                    kFrame, 1) != kFrame) {
        return fail("decoding the redundancy");
    }
    double recovered =
        correlation(&pcm[lost * kFrame - delay + kFrame / 2], &out[lost * kFrame + kFrame / 2],
                    kFrame / 2);
    std::printf("fec: recovered correlation %.3f\n", recovered);
    opus_decoder_destroy(dec);
    if (recovered < 0.7) {
        return fail("the recovered frame follows the lost one");
    }
    return true;
}

// Three packets merged into one decode exactly as they did apart, and
// padding changes neither the audio nor the unpadded size.
bool repacketizes_and_pads(const std::vector<opus_int16>& pcm) {
    Encoded encoded;
    if (!encode(pcm, 13, 20000, false, false, &encoded)) {
        return false;
    }
    OpusRepacketizer* rp = opus_repacketizer_create();
    for (int f = 10; f < 13; f++) {
        if (opus_repacketizer_cat(rp, &encoded.packets[f][0],
                                  static_cast<opus_int32>(encoded.packets[f].size())) != OPUS_OK) {
            return fail("opus_repacketizer_cat");
        }
    }
    unsigned char merged[3 * kMaxPacket + 64];
    opus_int32 len = opus_repacketizer_out(rp, merged, 3 * kMaxPacket);
    opus_repacketizer_destroy(rp);
    if (len < 1 || opus_packet_get_nb_frames(merged, len) != 3 ||
        opus_packet_get_nb_samples(merged, len, kRate) != 3 * kFrame) {
        return fail("a packet of the three frames");
    }

    OpusDecoder* dec[3];
    std::vector<opus_int16> out[3];
    int error = 0;
    for (int d = 0; d < 3; d++) {
        dec[d] = opus_decoder_create(kRate, 1, &error);
        out[d].resize(13 * kFrame);
        for (int f = 0; f < 13; f++) {
            if (dec[d] == NULL || (f < 10 && !decode(dec[d], encoded.packets[f], &out[d][f * kFrame]))) {
                return fail("decoder");
            }
        }
    }
    for (int f = 10; f < 13; f++) {
        if (!decode(dec[0], encoded.packets[f], &out[0][f * kFrame])) {
            return fail("opus_decode");
        }
    }
    bool ok = true;
    if (opus_decode(dec[1], merged, len, &out[1][10 * kFrame], 3 * kFrame, 0) != 3 * kFrame) {
        ok = fail("decoding the merged packet");
    }
    if (opus_packet_pad(merged, len, len + 40) != OPUS_OK ||
        opus_packet_get_nb_frames(merged, len + 40) != 3 ||
        opus_decode(dec[2], merged, len + 40, &out[2][10 * kFrame], 3 * kFrame, 0) != 3 * kFrame) {
        ok = fail("decoding the padded packet");
    }
    if (out[1] != out[0] || out[2] != out[0]) {
        ok = fail("merged and padded packets decode as their parts");
    }
    if (opus_packet_unpad(merged, len + 40) != len) {
        ok = fail("opus_packet_unpad");
    }
    for (int d = 0; d < 3; d++) {
        opus_decoder_destroy(dec[d]);
    }
    return ok;
}

// First-order ambisonics (ACN/SN3D, mapping family 3): the encoder mixes
// the four channels into two coupled streams and publishes the matrix
// that undoes it.  A tone on W alone comes back on W alone.
bool ambisonics_round_trip() {
    const int rate = 48000, frame = 960, channels = 4;
    int error = 0, streams = 0, coupled = 0;
    OpusProjectionEncoder* enc = opus_projection_ambisonics_encoder_create(
        rate, channels, 3, &streams, &coupled, OPUS_APPLICATION_AUDIO, &error);
    if (enc == NULL || error != OPUS_OK) {
        return fail("projection encoder");
    }
    opus_int32 size = 0;
    opus_projection_encoder_ctl(enc, OPUS_PROJECTION_GET_DEMIXING_MATRIX_SIZE(&size));
    bool ok = true;
    if (streams != 2 || coupled != 2 || size != channels * (streams + coupled) * 2) {
        ok = fail("four channels in two coupled streams, a 4x4 matrix of 16-bit gains");
    }
    std::vector<unsigned char> matrix(size);
    if (opus_projection_encoder_ctl(enc, OPUS_PROJECTION_GET_DEMIXING_MATRIX(&matrix[0], size)) !=
        OPUS_OK) {
        return fail("demixing matrix");
    }
    OpusProjectionDecoder* dec =
        opus_projection_decoder_create(rate, channels, streams, coupled, &matrix[0], size, &error);
    if (dec == NULL || error != OPUS_OK) {
        return fail("projection decoder");
    }
    std::vector<opus_int16> in(frame * channels), out(frame * channels);
    std::vector<unsigned char> packet(4 * kMaxPacket);
    double energy[4] = {0.0, 0.0, 0.0, 0.0};
    for (int f = 0; f < 10; f++) {
        for (int n = 0; n < frame; n++) {
            in[n * channels] =
                static_cast<opus_int16>(8000.0 * std::sin(2.0 * kPi * 440.0 * (f * frame + n) / rate));
        }
        int len = opus_projection_encode(enc, &in[0], frame, &packet[0],
                                         static_cast<opus_int32>(packet.size()));
        if (len < 1 || opus_projection_decode(dec, &packet[0], len, &out[0], frame, 0) != frame) {
            return fail("projection round trip");
        }
        for (int n = 0; f >= 2 && n < frame; n++) {
            for (int ch = 0; ch < channels; ch++) {
                energy[ch] += static_cast<double>(out[n * channels + ch]) * out[n * channels + ch];
            }
        }
    }
    std::printf("ambisonics: W/X/Y/Z energy %.3g %.3g %.3g %.3g\n", energy[0], energy[1], energy[2],
                energy[3]);
    for (int ch = 1; ch < channels; ch++) {
        if (energy[ch] * 100.0 > energy[0]) {
            ok = fail("the tone stays on W");
        }
    }
    if (energy[0] < 1e9) {
        ok = fail("W carries the tone");
    }
    opus_projection_encoder_destroy(enc);
    opus_projection_decoder_destroy(dec);
    return ok;
}

}  // namespace

int main() {
    const std::vector<opus_int16> pcm = vowel_then_noise();
    long delay = 0;
    bool ok = silk_round_trip(pcm, &delay);
    ok = dtx_follows_voice_activity(pcm) && ok;
    ok = fec_recovers_a_lost_frame(pcm, delay) && ok;
    ok = repacketizes_and_pads(pcm) && ok;
    ok = ambisonics_round_trip() && ok;
    return ok ? 0 : 1;
}
