/* The C consumer: a narrowband round trip of a 440 Hz tone at quality 8
 * through the integer API (frame size, bitrate, exact bits per frame,
 * decoded energy and pitch), the mode and library queries, and the
 * stream header. */
#include <speex/speex.h>
#include <speex/speex_header.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

#define PI 3.14159265358979323846
#define RATE 8000
#define FRAME 160
#define FRAMES 50
#define TONE 440.0

static spx_int16_t input[FRAMES * FRAME];
static spx_int16_t output[FRAMES * FRAME];

static int fail(const char *what) {
    fprintf(stderr, "FAIL: %s\n", what);
    return 0;
}

static int reports_its_version(void) {
    const char *version = NULL, *extra = NULL;
    int major = 0, minor = 0, micro = 0, ok = 1;
    speex_lib_ctl(SPEEX_LIB_GET_VERSION_STRING, &version);
    speex_lib_ctl(SPEEX_LIB_GET_EXTRA_VERSION, &extra);
    speex_lib_ctl(SPEEX_LIB_GET_MAJOR_VERSION, &major);
    speex_lib_ctl(SPEEX_LIB_GET_MINOR_VERSION, &minor);
    speex_lib_ctl(SPEEX_LIB_GET_MICRO_VERSION, &micro);
    if (version == NULL || strcmp(version, "1.2.1") != 0) {
        ok = fail("the version string is 1.2.1");
    }
    if (extra == NULL || strcmp(extra, "") != 0) {
        ok = fail("no extra version");
    }
    if (major != 1 || minor != 2 || micro != 1) {
        ok = fail("version 1, 2, 1");
    }
    return ok;
}

static int answers_mode_queries(void) {
    /* Bits per frame of narrowband submodes 0 to 9 (modes.c); 9 is unused. */
    static const int nb_bits[10] = {5, 43, 119, 160, 220, 300, 364, 492, 79, -1};
    int n, i, ok = 1;
    speex_mode_query(&speex_nb_mode, SPEEX_MODE_FRAME_SIZE, &n);
    if (n != 160) {
        ok = fail("a narrowband frame is 160 samples");
    }
    speex_mode_query(&speex_wb_mode, SPEEX_MODE_FRAME_SIZE, &n);
    if (n != 320) {
        ok = fail("a wideband frame is 320 samples");
    }
    speex_mode_query(&speex_uwb_mode, SPEEX_MODE_FRAME_SIZE, &n);
    if (n != 640) {
        ok = fail("an ultra-wideband frame is 640 samples");
    }
    for (i = 0; i < 10; i++) {
        n = i;
        speex_mode_query(&speex_nb_mode, SPEEX_SUBMODE_BITS_PER_FRAME, &n);
        if (n != nb_bits[i]) {
            ok = fail("narrowband submode bits per frame");
        }
    }
    if (speex_lib_get_mode(SPEEX_MODEID_NB) != &speex_nb_mode ||
        speex_lib_get_mode(SPEEX_MODEID_WB) != &speex_wb_mode ||
        speex_lib_get_mode(SPEEX_MODEID_UWB) != &speex_uwb_mode) {
        ok = fail("speex_lib_get_mode");
    }
    if (speex_nb_mode.modeID != 0 || speex_wb_mode.modeID != 1 || speex_uwb_mode.modeID != 2 ||
        speex_nb_mode.bitstream_version != 4 || strcmp(speex_nb_mode.modeName, "narrowband") != 0) {
        ok = fail("mode identities");
    }
    return ok;
}

static int writes_the_stream_header(void) {
    SpeexHeader header, *parsed;
    char *packet;
    int size = 0, ok = 1;
    speex_init_header(&header, RATE, 1, &speex_nb_mode);
    if (memcmp(header.speex_string, "Speex   ", 8) != 0 ||
        strcmp(header.speex_version, "1.2.1") != 0) {
        ok = fail("the header names Speex 1.2.1");
    }
    if (header.speex_version_id != 1 || header.header_size != 80 || header.rate != RATE ||
        header.mode != 0 || header.mode_bitstream_version != 4 || header.nb_channels != 1 ||
        header.bitrate != -1 || header.frame_size != 160 || header.vbr != 0 ||
        header.frames_per_packet != 0 || header.extra_headers != 0) {
        ok = fail("speex_init_header's fields");
    }
    packet = speex_header_to_packet(&header, &size);
    /* The rate, a little-endian 32-bit field at offset 36. */
    if (packet == NULL || size != 80 || memcmp(packet, "Speex   1.2.1", 13) != 0 ||
        (unsigned char)packet[36] != 0x40 || (unsigned char)packet[37] != 0x1f ||
        packet[38] != 0 || packet[39] != 0) {
        return fail("speex_header_to_packet");
    }
    parsed = speex_packet_to_header(packet, size);
    if (parsed == NULL || parsed->rate != RATE || parsed->frame_size != 160 || parsed->mode != 0) {
        ok = fail("speex_packet_to_header");
    }
    if (speex_packet_to_header(packet, 79) != NULL) {
        ok = fail("a short header is rejected");
    }
    speex_header_free(packet);
    speex_header_free(parsed);
    return ok;
}

/* The share of x[0..n)'s energy at TONE: 1 for a pure tone on a bin. */
static double tone_share(const spx_int16_t *x, int n) {
    double re = 0.0, im = 0.0, energy = 0.0;
    int i;
    for (i = 0; i < n; i++) {
        re += x[i] * cos(2.0 * PI * TONE * i / RATE);
        im += x[i] * sin(2.0 * PI * TONE * i / RATE);
        energy += (double)x[i] * x[i];
    }
    return 2.0 * (re * re + im * im) / (n * energy + 1.0);
}

static double energy_of(const spx_int16_t *x, int n) {
    double energy = 0.0;
    int i;
    for (i = 0; i < n; i++) {
        energy += (double)x[i] * x[i];
    }
    return energy;
}

static int narrowband_round_trip(void) {
    void *enc = speex_encoder_init(&speex_nb_mode);
    void *dec = speex_decoder_init(&speex_nb_mode);
    SpeexBits ebits, dbits;
    char packet[200];
    spx_int16_t scratch[FRAME];
    spx_int32_t quality = 8, value = 0;
    int frame, i, ok = 1;
    double ratio, share;
    if (enc == NULL || dec == NULL) {
        return fail("init");
    }
    for (i = 0; i < FRAMES * FRAME; i++) {
        input[i] = (spx_int16_t)floor(8000.0 * sin(2.0 * PI * TONE * i / RATE) + 0.5);
    }
    speex_encoder_ctl(enc, SPEEX_GET_FRAME_SIZE, &value);
    if (value != FRAME) {
        ok = fail("the encoder takes 160-sample frames");
    }
    speex_decoder_ctl(dec, SPEEX_GET_FRAME_SIZE, &value);
    if (value != FRAME) {
        ok = fail("the decoder gives 160-sample frames");
    }
    speex_encoder_ctl(enc, SPEEX_SET_QUALITY, &quality);
    speex_encoder_ctl(enc, SPEEX_GET_MODE, &value);
    if (value != 5) {
        ok = fail("quality 8 selects submode 5");
    }
    speex_encoder_ctl(enc, SPEEX_GET_BITRATE, &value);
    if (value != 15000) {
        ok = fail("submode 5 is 15 kb/s");
    }
    speex_encoder_ctl(enc, SPEEX_GET_SAMPLING_RATE, &value);
    if (value != RATE) {
        ok = fail("narrowband samples at 8 kHz");
    }
    speex_encoder_ctl(enc, SPEEX_GET_LOOKAHEAD, &value);
    if (value != 40) {
        ok = fail("the encoder looks 40 samples ahead");
    }

    speex_bits_init(&ebits);
    speex_bits_init(&dbits);
    for (frame = 0; frame < FRAMES; frame++) {
        int nbytes;
        speex_bits_reset(&ebits);
        if (speex_encode_int(enc, input + frame * FRAME, &ebits) != 1) {
            return fail("speex_encode_int");
        }
        if (ebits.nbBits != 300 || speex_bits_nbytes(&ebits) != 38) {
            ok = fail("a submode 5 frame is 300 bits in 38 bytes");
        }
        nbytes = speex_bits_write(&ebits, packet, (int)sizeof packet);
        if (nbytes != 38) {
            return fail("speex_bits_write");
        }
        speex_bits_read_from(&dbits, packet, nbytes);
        if (speex_decode_int(dec, &dbits, output + frame * FRAME) != 0) {
            return fail("speex_decode_int");
        }
        /* The decoder reads the 300 bits and leaves the 4-bit terminator. */
        if (speex_bits_remaining(&dbits) != 4) {
            ok = fail("the decoder reads one frame's bits");
        }
        if (speex_decode_int(dec, &dbits, scratch) != -1) {
            ok = fail("the terminator ends the packet");
        }
    }

    /* Over the last half second: the tone comes back at its level. */
    ratio = energy_of(output + FRAMES / 2 * FRAME, FRAMES / 2 * FRAME) /
            energy_of(input + FRAMES / 2 * FRAME, FRAMES / 2 * FRAME);
    share = tone_share(output + FRAMES / 2 * FRAME, FRAMES / 2 * FRAME);
    printf("narrowband: energy ratio %.3f, tone share %.3f\n", ratio, share);
    if (ratio < 0.5 || ratio > 2.0) {
        ok = fail("the decoded energy is within a factor 2 of the input's");
    }
    if (share < 0.9) {
        ok = fail("the decoded signal is the 440 Hz tone");
    }
    speex_bits_destroy(&ebits);
    speex_bits_destroy(&dbits);
    speex_encoder_destroy(enc);
    speex_decoder_destroy(dec);
    return ok;
}

int main(void) {
    int ok = reports_its_version();
    ok = answers_mode_queries() && ok;
    ok = writes_the_stream_header() && ok;
    ok = narrowband_round_trip() && ok;
    return ok ? 0 : 1;
}
