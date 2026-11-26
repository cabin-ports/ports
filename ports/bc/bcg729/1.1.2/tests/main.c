/* Round-trips a sine through the G.729A encoder and decoder, then checks
 * that VAD/DTX stops transmitting once the input turns to silence. */
#include <bcg729/decoder.h>
#include <bcg729/encoder.h>

#include <stdio.h>
#include <string.h>

#define FRAMES 100
#define FRAME 80
#define SKIP (10 * FRAME) /* codec warm-up */

/* One period of a 500 Hz sine at 8 kHz, amplitude 8000; avoids linking libm. */
static const int16_t period[16] = {0, 3061, 5657, 7391, 8000, 7391, 5657, 3061,
                                   0, -3061, -5657, -7391, -8000, -7391, -5657, -3061};

static int ok = 1;

static void check(int pass, const char *what)
{
    if (!pass) {
        fprintf(stderr, "FAIL: %s\n", what);
        ok = 0;
    }
}

int main(void)
{
    static int16_t pcm[FRAMES * FRAME], out[FRAMES * FRAME], out2[FRAMES * FRAME], zero[FRAME];
    static uint8_t bits[FRAMES * 10];
    bcg729EncoderChannelContextStruct *enc = initBcg729EncoderChannel(0);
    bcg729DecoderChannelContextStruct *dec = initBcg729DecoderChannel();
    bcg729DecoderChannelContextStruct *dec2 = initBcg729DecoderChannel();
    bcg729EncoderChannelContextStruct *vad = initBcg729EncoderChannel(1);
    double ein = 0, eout = 0;
    int i, lag, correlated = 0, silent = 0, sid = 0, voiced = 0;

    check(enc && dec && dec2 && vad, "channel creation");
    for (i = 0; i < FRAMES * FRAME; i++)
        pcm[i] = period[i % 16];

    for (i = 0; i < FRAMES; i++) {
        uint8_t len = 0;
        bcg729Encoder(enc, pcm + i * FRAME, bits + i * 10, &len);
        check(len == 10, "a voice frame is 10 bytes");
        bcg729Decoder(dec, bits + i * 10, 10, 0, 0, 0, out + i * FRAME);
        bcg729Decoder(dec2, bits + i * 10, 10, 0, 0, 0, out2 + i * FRAME);
    }
    check(memcmp(out, out2, sizeof out) == 0, "decoding is deterministic");

    for (i = SKIP; i < FRAMES * FRAME; i++) {
        ein += (double)pcm[i] * pcm[i];
        eout += (double)out[i] * out[i];
    }
    check(eout / ein > 0.8 && eout / ein < 1.0, "decoded energy near the input's");

    /* The decoder delays its output by a fixed number of samples. */
    for (lag = 0; lag <= FRAME; lag++) {
        double c = 0, a = 0, b = 0;
        for (i = SKIP; i < FRAMES * FRAME - FRAME; i++) {
            c += (double)pcm[i] * out[i + lag];
            a += (double)pcm[i] * pcm[i];
            b += (double)out[i + lag] * out[i + lag];
        }
        if (c > 0 && c * c > 0.98 * 0.98 * a * b)
            correlated = 1;
    }
    check(correlated, "decoded signal correlates with the input");

    for (i = 0; i < 2 * FRAMES; i++) {
        uint8_t len = 99, b[10];
        bcg729Encoder(vad, i < FRAMES ? pcm + i * FRAME : zero, b, &len);
        if (i < FRAMES)
            voiced += len == 10;
        else if (len == 0)
            silent++;
        else if (len == 2)
            sid++;
        else
            check(len == 10, "frame lengths are 0, 2 or 10 with VAD");
    }
    check(voiced > 90, "the sine is transmitted as voice");
    check(sid >= 1 && silent > 50, "silence becomes SID and untransmitted frames");

    closeBcg729EncoderChannel(enc);
    closeBcg729EncoderChannel(vad);
    closeBcg729DecoderChannel(dec);
    closeBcg729DecoderChannel(dec2);
    return !ok;
}
