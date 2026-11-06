/* The C consumer, in C89 like the headers: CELT round trips of a periodic
 * stereo signal (fidelity, the decoder's postfilter pitch, packet loss
 * concealment, and encoder/decoder range-coder agreement), packet
 * inspection and rejection, and a 5.1 surround multistream round trip. */
#include <opus.h>
#include <opus_multistream.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

#define PI 3.14159265358979323846
#define RATE 48000
#define FRAME 960            /* 20 ms */
#define FRAMES 50
#define FUNDAMENTAL 200      /* Hz */
#define PERIOD (RATE / FUNDAMENTAL)
#define MAX_PACKET 1500

static opus_int16 input[(FRAMES + 1) * FRAME * 2];
static opus_int16 output[(FRAMES + 1) * FRAME * 2];
static opus_int16 surround_in[FRAME * 6];
static opus_int16 surround_out[FRAME * 6];

static int fail(const char *what) {
    fprintf(stderr, "FAIL: %s\n", what);
    return 0;
}

/* Harmonics of FUNDAMENTAL with 1/k amplitudes; odd ones only for the
 * right channel, so a channel swap shows. */
static double periodic(long n, int odd_only) {
    double t = (double)n / RATE;
    double sum = 0.0;
    int k;
    for (k = 1; k <= 12; k++) {
        if (!odd_only || k % 2 == 1) {
            sum += sin(2.0 * PI * FUNDAMENTAL * k * t) / k;
        }
    }
    return sum;
}

/* The SNR, in dB, of channel ch of out[] against ref[] delayed by lag
 * samples, over out[from..to). */
static double snr_db(const opus_int16 *ref, const opus_int16 *out, int channels, int ch,
                     long from, long to, long lag) {
    double signal = 0.0, noise = 0.0;
    long i;
    for (i = from; i < to; i++) {
        double r = ref[(i - lag) * channels + ch];
        double d = r - out[i * channels + ch];
        signal += r * r;
        noise += d * d;
    }
    return 10.0 * log10(signal / (noise + 1.0));
}

static double correlation(const opus_int16 *a, const opus_int16 *b, int channels, int ch,
                          long from, long to, long lag) {
    double ab = 0.0, aa = 0.0, bb = 0.0;
    long i;
    for (i = from; i < to; i++) {
        double x = a[(i - lag) * channels + ch];
        double y = b[i * channels + ch];
        ab += x * y;
        aa += x * x;
        bb += y * y;
    }
    return ab / sqrt(aa * bb + 1.0);
}

static int reports_its_version_and_errors(void) {
    int error = 0;
    OpusEncoder *enc;
    if (strcmp(opus_get_version_string(), "libopus 1.6.1") != 0) {
        return fail(opus_get_version_string());
    }
    if (strcmp(opus_strerror(OPUS_BAD_ARG), "invalid argument") != 0) {
        return fail("opus_strerror");
    }
    enc = opus_encoder_create(44100, 2, OPUS_APPLICATION_AUDIO, &error);
    if (enc != NULL || error != OPUS_BAD_ARG) {
        return fail("44.1 kHz is not an Opus rate");
    }
    return 1;
}

/* Encodes FRAMES frames with CELT alone (restricted low delay), checks
 * every packet, and decodes them; then conceals one lost frame. */
static int celt_round_trip(void) {
    OpusEncoder *enc;
    OpusDecoder *dec;
    unsigned char packet[MAX_PACKET];
    int error = 0, lookahead = 0, frame, ok = 1;
    opus_int32 pitch = 0;
    opus_uint32 enc_range, dec_range;
    long n, lag, best_lag = 0;
    double snr, best = -100.0, left, right, concealed;

    for (n = 0; n < (FRAMES + 1) * FRAME; n++) {
        input[2 * n] = (opus_int16)(9000.0 * periodic(n, 0));
        input[2 * n + 1] = (opus_int16)(9000.0 * periodic(n, 1));
    }
    enc = opus_encoder_create(RATE, 2, OPUS_APPLICATION_RESTRICTED_LOWDELAY, &error);
    dec = opus_decoder_create(RATE, 2, &error);
    if (enc == NULL || dec == NULL || error != OPUS_OK) {
        return fail("create");
    }
    if (opus_encoder_ctl(enc, OPUS_SET_BITRATE(96000)) != OPUS_OK ||
        opus_encoder_ctl(enc, OPUS_SET_COMPLEXITY(10)) != OPUS_OK ||
        opus_encoder_ctl(enc, OPUS_GET_LOOKAHEAD(&lookahead)) != OPUS_OK) {
        return fail("encoder ctl");
    }
    if (lookahead != RATE / 400) {
        ok = fail("restricted low delay looks 2.5 ms ahead");
    }
    for (frame = 0; frame < FRAMES; frame++) {
        int len = opus_encode(enc, input + frame * FRAME * 2, FRAME, packet, MAX_PACKET);
        if (len < 3) {
            return fail("opus_encode");
        }
        if ((packet[0] >> 3) < 16 || opus_packet_get_bandwidth(packet) != OPUS_BANDWIDTH_FULLBAND ||
            opus_packet_get_nb_channels(packet) != 2 || opus_packet_get_nb_frames(packet, len) != 1 ||
            opus_packet_get_nb_samples(packet, len, RATE) != FRAME) {
            ok = fail("a fullband stereo CELT packet of one 20 ms frame");
        }
        if (opus_decode(dec, packet, len, output + frame * FRAME * 2, FRAME, 0) != FRAME) {
            return fail("opus_decode");
        }
        opus_encoder_ctl(enc, OPUS_GET_FINAL_RANGE(&enc_range));
        opus_decoder_ctl(dec, OPUS_GET_FINAL_RANGE(&dec_range));
        if (enc_range != dec_range) {
            ok = fail("the decoder's range coder ends where the encoder's did");
        }
    }

    /* Align the output with the input over the last half second. */
    for (lag = 0; lag <= 4 * lookahead; lag++) {
        snr = snr_db(input, output, 2, 0, FRAMES / 2 * FRAME, FRAMES * FRAME, lag);
        if (snr > best) {
            best = snr;
            best_lag = lag;
        }
    }
    left = snr_db(input, output, 2, 0, FRAMES / 2 * FRAME, FRAMES * FRAME, best_lag);
    right = snr_db(input, output, 2, 1, FRAMES / 2 * FRAME, FRAMES * FRAME, best_lag);
    opus_decoder_ctl(dec, OPUS_GET_PITCH(&pitch));

    /* Frame FRAMES is lost: the decoder extends the waveform by its pitch. */
    if (opus_decode(dec, NULL, 0, output + FRAMES * FRAME * 2, FRAME, 0) != FRAME) {
        return fail("concealment");
    }
    concealed = correlation(input, output, 2, 0, FRAMES * FRAME, FRAMES * FRAME + FRAME / 2,
                            best_lag);
    printf("celt: delay %ld, snr %.1f/%.1f dB, pitch %ld, concealed correlation %.3f\n",
           best_lag, left, right, (long)pitch, concealed);
    if (best_lag != lookahead) {
        ok = fail("the output lags the input by the lookahead");
    }
    if (left < 15.0 || right < 15.0) {
        ok = fail("96 kb/s CELT keeps both channels above 15 dB SNR");
    }
    if (pitch < PERIOD - 2 || pitch > PERIOD + 2) {
        ok = fail("the postfilter runs at the input's period");
    }
    if (concealed < 0.8) {
        ok = fail("the concealed frame continues the waveform");
    }
    opus_encoder_destroy(enc);
    opus_decoder_destroy(dec);
    return ok;
}

static int rejects_malformed_packets(void) {
    /* Code 3 (an explicit frame count) with a count of zero. */
    static const unsigned char no_frames[] = {0x03, 0x00};
    /* Code 1 (two frames of equal size) with a one-byte payload. */
    static const unsigned char odd_split[] = {0xf9, 0x01};
    opus_int16 pcm[FRAME * 2];
    int error = 0, ok = 1;
    OpusDecoder *dec = opus_decoder_create(RATE, 2, &error);
    if (dec == NULL) {
        return fail("create");
    }
    if (opus_decode(dec, no_frames, sizeof no_frames, pcm, FRAME, 0) != OPUS_INVALID_PACKET) {
        ok = fail("a packet of zero frames");
    }
    if (opus_packet_get_nb_frames(no_frames, 1) != OPUS_INVALID_PACKET) {
        ok = fail("a code 3 packet without its frame count");
    }
    if (opus_decode(dec, odd_split, sizeof odd_split, pcm, FRAME, 0) != OPUS_INVALID_PACKET) {
        ok = fail("an odd split of two equal frames");
    }
    if (opus_decode(dec, NULL, 0, pcm, 100, 0) != OPUS_BAD_ARG) {
        ok = fail("concealing a frame that is not a multiple of 2.5 ms");
    }
    opus_decoder_destroy(dec);
    return ok;
}

/* 5.1 in Vorbis channel order (L, C, R, rear L, rear R, LFE): a tone on
 * the centre channel alone stays there through four streams. */
static int surround_round_trip(void) {
    OpusMSEncoder *enc;
    OpusMSDecoder *dec;
    unsigned char packet[MAX_PACKET * 4];
    unsigned char mapping[6];
    double energy[6];
    int error = 0, streams = 0, coupled = 0, frame, ch, ok = 1;
    long n;
    enc = opus_multistream_surround_encoder_create(RATE, 6, 1, &streams, &coupled, mapping,
                                                   OPUS_APPLICATION_AUDIO, &error);
    if (enc == NULL || error != OPUS_OK) {
        return fail("surround encoder");
    }
    if (streams != 4 || coupled != 2) {
        ok = fail("5.1 codes as four streams, two of them coupled");
    }
    dec = opus_multistream_decoder_create(RATE, 6, streams, coupled, mapping, &error);
    if (dec == NULL || error != OPUS_OK) {
        return fail("multistream decoder");
    }
    for (ch = 0; ch < 6; ch++) {
        energy[ch] = 0.0;
    }
    for (frame = 0; frame < 10; frame++) {
        int len;
        memset(surround_in, 0, sizeof surround_in);
        for (n = 0; n < FRAME; n++) {
            surround_in[n * 6 + 1] =
                (opus_int16)(8000.0 * sin(2.0 * PI * 440.0 * (frame * FRAME + n) / RATE));
        }
        len = opus_multistream_encode(enc, surround_in, FRAME, packet, sizeof packet);
        if (len < 1 ||
            opus_multistream_decode(dec, packet, len, surround_out, FRAME, 0) != FRAME) {
            return fail("multistream round trip");
        }
        for (n = FRAME / 2; frame >= 2 && n < FRAME; n++) {
            for (ch = 0; ch < 6; ch++) {
                energy[ch] += (double)surround_out[n * 6 + ch] * surround_out[n * 6 + ch];
            }
        }
    }
    for (ch = 0; ch < 6; ch++) {
        if (ch != 1 && energy[ch] * 1000.0 > energy[1]) {
            ok = fail("only the centre channel carries the tone");
        }
    }
    if (energy[1] < 1e9) {
        ok = fail("the centre channel carries the tone");
    }
    opus_multistream_encoder_destroy(enc);
    opus_multistream_decoder_destroy(dec);
    return ok;
}

int main(void) {
    int ok = reports_its_version_and_errors();
    ok = celt_round_trip() && ok;
    ok = rejects_malformed_packets() && ok;
    ok = surround_round_trip() && ok;
    return ok ? 0 : 1;
}
