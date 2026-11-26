/* The C consumer: one second of a 1 kHz stereo tone through an AAC-LC ADTS
 * round trip (fdk-aac encoder and decoder), the libraries' version reports,
 * and an xHE-AAC (USAC over LOAS) encode by fdk-xhe-aac that the fdk-aac
 * decoder plays back. */
#include <fdk-aac/aacdecoder_lib.h>
#include <fdk-aac/aacenc_lib.h>
#include <fdk-xhe-aac/xHEAACEnc.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

#define PI 3.14159265358979323846
#define RATE 44100
#define TONE 1000.0
#define FRAMES 44100L /* per channel: one second */
/* 600 periods of the tone, from 0.2 s in: past the codec delay and start-up. */
#define WIN_FROM 8820L
#define WIN_LEN 26460L

static INT_PCM pcm[FRAMES * 2];
static float pcm_float[FRAMES * 2];
static unsigned char stream[1 << 20];
static INT_PCM decoded[FRAMES * 2 + 65536];
static int failures = 0;

static void check(int ok, const char *what) {
    if (!ok) {
        fprintf(stderr, "FAIL: %s\n", what);
        failures++;
    }
}

/* Left at amplitude 0.2, right at 0.1, so a channel swap shows. */
static double tone(long n, int ch) {
    return (ch ? 0.1 : 0.2) * sin(2.0 * PI * TONE * n / RATE);
}

/* Mean square of channel ch of x[] over the window, shifted by lag. */
static double mean_square(const INT_PCM *x, int ch, long lag) {
    double sum = 0.0;
    long i;
    for (i = WIN_FROM; i < WIN_FROM + WIN_LEN; i++) {
        double v = x[(i + lag) * 2 + ch] / 32768.0;
        sum += v * v;
    }
    return sum / WIN_LEN;
}

/* The share of channel ch's power in the window that is the 1 kHz tone. */
static double tone_share(const INT_PCM *x, int ch) {
    double re = 0.0, im = 0.0, amp;
    long i;
    for (i = WIN_FROM; i < WIN_FROM + WIN_LEN; i++) {
        double v = x[i * 2 + ch] / 32768.0;
        re += v * cos(2.0 * PI * TONE * i / RATE);
        im += v * sin(2.0 * PI * TONE * i / RATE);
    }
    amp = 2.0 * sqrt(re * re + im * im) / WIN_LEN;
    return amp * amp / 2.0 / mean_square(x, ch, 0);
}

/* The SNR, in dB, of channel ch of decoded[] against pcm[] delayed by lag. */
static double snr_db(int ch, long lag) {
    double signal = 0.0, noise = 0.0;
    long i;
    for (i = WIN_FROM; i < WIN_FROM + WIN_LEN; i++) {
        double r = pcm[i * 2 + ch];
        double d = r - decoded[(i + lag) * 2 + ch];
        signal += r * r;
        noise += d * d;
    }
    return 10.0 * log10(signal / (noise + 1.0));
}

static const LIB_INFO *find_module(const LIB_INFO *info, FDK_MODULE_ID id) {
    int i;
    for (i = 0; i < FDK_MODULE_LAST && info[i].module_id != FDK_NONE; i++) {
        if (info[i].module_id == id) {
            return &info[i];
        }
    }
    return NULL;
}

static void check_versions(void) {
    LIB_INFO info[FDK_MODULE_LAST];
    const LIB_INFO *m;

    FDKinitLibInfo(info);
    check(aacEncGetLibInfo(info) == AACENC_OK, "aacEncGetLibInfo");
    m = find_module(info, FDK_AACENC);
    check(m != NULL && m->version == LIB_VERSION(4, 0, 1) &&
              strcmp(m->versionStr, "4.0.1") == 0 && strcmp(m->title, "AAC Encoder") == 0,
          "aacEncGetLibInfo reports AAC Encoder 4.0.1");
    check(LIB_VERSION(AACENCODER_LIB_VL0, AACENCODER_LIB_VL1, AACENCODER_LIB_VL2) ==
              LIB_VERSION(4, 0, 1),
          "aacenc_lib.h says 4.0.1");

    FDKinitLibInfo(info);
    check(aacDecoder_GetLibInfo(info) == 0, "aacDecoder_GetLibInfo");
    m = find_module(info, FDK_AACDEC);
    check(m != NULL && m->version == LIB_VERSION(3, 2, 0) &&
              strcmp(m->versionStr, "3.2.0") == 0 && strcmp(m->title, "AAC Decoder Lib") == 0,
          "aacDecoder_GetLibInfo reports AAC Decoder Lib 3.2.0");
    check(LIB_VERSION(AACDECODER_LIB_VL0, AACDECODER_LIB_VL1, AACDECODER_LIB_VL2) ==
              LIB_VERSION(3, 2, 0),
          "aacdecoder_lib.h says 3.2.0");

    check(aacEncGetLibInfo(NULL) == AACENC_INVALID_HANDLE, "aacEncGetLibInfo(NULL)");
    check(aacDecoder_GetLibInfo(NULL) == -1, "aacDecoder_GetLibInfo(NULL)");
    check(strcmp(IIS_xHEAACEnc_GetVersionInfoStr(), "1.0AOSP") == 0,
          "IIS_xHEAACEnc_GetVersionInfoStr is 1.0AOSP");
}

/* ADTS header fields (ISO/IEC 13818-7): AAC LC, 44.1 kHz, two channels,
 * and a frame length that covers the whole encoder output. */
static int adts_fields_ok(const unsigned char *h, int size) {
    int layer = (h[1] >> 1) & 3;
    int profile = h[2] >> 6;
    int rate_index = (h[2] >> 2) & 15;
    int channels = ((h[2] & 1) << 2) | (h[3] >> 6);
    int length = ((h[3] & 3) << 11) | (h[4] << 3) | (h[5] >> 5);
    return layer == 0 && profile == 1 && rate_index == 4 && channels == 2 && length == size;
}

/* Encodes pcm[] to ADTS AAC-LC at 128 kb/s into stream[]; returns its size. */
static long encode_adts(UINT *delay, int *frames_out) {
    HANDLE_AACENCODER enc = NULL;
    AACENC_InfoStruct info;
    long used = 0, fed = 0;
    int i, frames = 0, bad_sync = 0, bad_fields = 0;

    check(aacEncOpen(&enc, 0, 2) == AACENC_OK, "aacEncOpen");
    check(aacEncoder_SetParam(enc, AACENC_AOT, AOT_AAC_LC) == AACENC_OK, "set AACENC_AOT");
    check(aacEncoder_SetParam(enc, AACENC_SAMPLERATE, RATE) == AACENC_OK,
          "set AACENC_SAMPLERATE");
    check(aacEncoder_SetParam(enc, AACENC_CHANNELMODE, MODE_2) == AACENC_OK,
          "set AACENC_CHANNELMODE");
    check(aacEncoder_SetParam(enc, AACENC_CHANNELORDER, 1) == AACENC_OK,
          "set AACENC_CHANNELORDER");
    check(aacEncoder_SetParam(enc, AACENC_BITRATE, 128000) == AACENC_OK, "set AACENC_BITRATE");
    check(aacEncoder_SetParam(enc, AACENC_TRANSMUX, TT_MP4_ADTS) == AACENC_OK,
          "set AACENC_TRANSMUX");
    check(aacEncoder_SetParam(enc, AACENC_AFTERBURNER, 1) == AACENC_OK,
          "set AACENC_AFTERBURNER");
    check(aacEncEncode(enc, NULL, NULL, NULL, NULL) == AACENC_OK, "encoder initialisation");
    check(aacEncInfo(enc, &info) == AACENC_OK, "aacEncInfo");
    check(info.frameLength == 1024 && info.inputChannels == 2,
          "encoder frame length 1024 with 2 input channels");
    /* AudioSpecificConfig: object type 2, frequency index 4, 2 channels. */
    check(info.confSize == 2 && info.confBuf[0] == 0x12 && info.confBuf[1] == 0x10,
          "AudioSpecificConfig 12 10");
    *delay = info.nDelay;

    for (i = 0; i < 1000; i++) {
        AACENC_BufDesc in_buf, out_buf;
        AACENC_InArgs in_args;
        AACENC_OutArgs out_args;
        void *in_ptr = pcm + fed * 2, *out_ptr = stream + used;
        INT in_id = IN_AUDIO_DATA, in_size, in_el = sizeof(INT_PCM);
        INT out_id = OUT_BITSTREAM_DATA, out_size = (INT)(sizeof stream - used), out_el = 1;
        long chunk = FRAMES - fed < 1024 ? FRAMES - fed : 1024;
        AACENC_ERROR err;

        memset(&in_args, 0, sizeof in_args);
        memset(&out_args, 0, sizeof out_args);
        in_size = (INT)(chunk * 2 * sizeof(INT_PCM));
        in_buf.numBufs = 1;
        in_buf.bufs = &in_ptr;
        in_buf.bufferIdentifiers = &in_id;
        in_buf.bufSizes = &in_size;
        in_buf.bufElSizes = &in_el;
        out_buf.numBufs = 1;
        out_buf.bufs = &out_ptr;
        out_buf.bufferIdentifiers = &out_id;
        out_buf.bufSizes = &out_size;
        out_buf.bufElSizes = &out_el;
        in_args.numInSamples = chunk > 0 ? (INT)(chunk * 2) : -1; /* -1 flushes */

        err = aacEncEncode(enc, &in_buf, &out_buf, &in_args, &out_args);
        if (err == AACENC_ENCODE_EOF) {
            break;
        }
        if (err != AACENC_OK) {
            check(0, "aacEncEncode returns AACENC_OK until AACENC_ENCODE_EOF");
            break;
        }
        fed += out_args.numInSamples / 2;
        if (out_args.numOutBytes > 0) {
            const unsigned char *h = stream + used;
            frames++;
            /* The syncword, twelve one bits. */
            if (out_args.numOutBytes < 7 || h[0] != 0xFF || (h[1] & 0xF0) != 0xF0) {
                bad_sync++;
            } else if (!adts_fields_ok(h, out_args.numOutBytes)) {
                bad_fields++;
            }
            used += out_args.numOutBytes;
        }
    }
    check(fed == FRAMES, "the encoder consumed every input sample");
    check(frames > 0 && used > 0, "the encoder produced ADTS frames");
    check(bad_sync == 0, "every ADTS frame starts with the syncword 0xFFF");
    check(bad_fields == 0, "every ADTS header says AAC LC, 44.1 kHz, stereo and its length");
    check(aacEncClose(&enc) == AACENC_OK && enc == NULL, "aacEncClose");
    printf("adts: %d frames, %ld bytes, delay %u\n", frames, used, *delay);
    *frames_out = frames;
    return used;
}

/* Decodes size bytes of stream[] into decoded[]; returns the frames decoded
 * and copies the first frame's stream info. */
static int decode(TRANSPORT_TYPE transport, long size, CStreamInfo *first) {
    HANDLE_AACDECODER dec = aacDecoder_Open(transport, 1);
    UINT left = (UINT)size;
    long got = 0;
    int frames = 0, i;

    check(dec != NULL, "aacDecoder_Open");
    check(aacDecoder_SetParam(dec, AAC_PCM_LIMITER_ENABLE, 0) == AAC_DEC_OK,
          "set AAC_PCM_LIMITER_ENABLE");
    memset(first, 0, sizeof *first);
    for (i = 0; i < 10000; i++) {
        AAC_DECODER_ERROR err;
        if (left > 0) {
            UCHAR *ptr = stream + (size - left);
            UINT buffer_size = left;
            check(aacDecoder_Fill(dec, &ptr, &buffer_size, &left) == AAC_DEC_OK,
                  "aacDecoder_Fill");
        }
        err = aacDecoder_DecodeFrame(dec, decoded + got * 2,
                                     (INT)(sizeof decoded / sizeof decoded[0] - got * 2), 0);
        if (err == AAC_DEC_NOT_ENOUGH_BITS) {
            if (left == 0) {
                break;
            }
            continue;
        }
        if (err != AAC_DEC_OK) {
            fprintf(stderr, "aacDecoder_DecodeFrame: 0x%x\n", (unsigned)err);
            check(0, "aacDecoder_DecodeFrame returns AAC_DEC_OK");
            break;
        }
        {
            CStreamInfo *info = aacDecoder_GetStreamInfo(dec);
            if (frames++ == 0) {
                *first = *info;
            }
            got += info->frameSize;
        }
    }
    printf("decode: %d frames, %ld samples per channel\n", frames, got);
    aacDecoder_Close(dec);
    return frames;
}

static void aac_lc_round_trip(void) {
    UINT delay = 0;
    int frames = 0, ch;
    long size = encode_adts(&delay, &frames), got, lag;
    CStreamInfo info;

    check(decode(TT_MP4_ADTS, size, &info) == frames, "the decoder decoded every ADTS frame");
    got = frames * 1024L;
    check(info.sampleRate == 44100 && info.numChannels == 2 && info.frameSize == 1024,
          "ADTS stream info: 44100 Hz, 2 channels, 1024 samples a frame");
    check(info.aot == AOT_AAC_LC, "ADTS stream info: AAC LC");
    /* The decoder delays its output by outputDelay beyond the encoder's delay. */
    lag = (long)delay + (long)info.outputDelay;
    check(got >= WIN_FROM + WIN_LEN + lag, "the decoder returned the comparison window");
    for (ch = 0; ch < 2 && got >= WIN_FROM + WIN_LEN + lag; ch++) {
        double ratio = mean_square(decoded, ch, lag) / mean_square(pcm, ch, 0);
        double snr = snr_db(ch, lag);
        printf("aac-lc channel %d: energy ratio %.5f, SNR %.2f dB\n", ch, ratio, snr);
        /* 128 kb/s keeps a pure tone's energy within 2% and its SNR above 40 dB. */
        check(fabs(ratio - 1.0) <= 0.02, "AAC-LC decoded energy within 2% of the input");
        check(snr >= 40.0, "AAC-LC decoded tone at least 40 dB above the coding noise");
    }
}

static void xhe_round_trip(void) {
    IIS_XHEAACENC_CONFIG_INSTANCE_HANDLE cfg = NULL;
    IIS_XHEAACENC_INSTANCE_HANDLE enc = NULL;
    IIS_XHEAACENC_ENCODER_STATE state = IIS_XHEAACENC_ENCODER_STATE_STARTUP;
    IIS_XHEAACENC_RETURN_CODE ret;
    int frame_size = 0, i, frames = 0, bad_sync = 0, bad_length = 0;
    long fed = 0, used = 0, got;
    CStreamInfo info;
    int decoded_frames;

    check(IIS_xHEAACEnc_Config_Open(&cfg, RATE, IIS_XHEAACENC_CHANNELCONFIG_STEREO) ==
              IIS_XHEAACENC_NO_ERROR,
          "IIS_xHEAACEnc_Config_Open");
    check(IIS_xHEAACEnc_Config_AddParamValueInt(cfg, IIS_XHEAACENC_PARAMETER_BITRATE, 64000) ==
              IIS_XHEAACENC_NO_ERROR,
          "set the xHE-AAC bitrate");
    check(IIS_xHEAACEnc_Config_AddParamValueInt(cfg, IIS_XHEAACENC_PARAMETER_BITRATEMODE,
                                                IIS_XHEAACENC_BITRATEMODE_CBR) ==
              IIS_XHEAACENC_NO_ERROR,
          "set the xHE-AAC bitrate mode");
    check(IIS_xHEAACEnc_Config_AddParamValueInt(cfg, IIS_XHEAACENC_PARAMETER_AOT,
                                                IIS_XHEAACENC_AOT_USAC) == IIS_XHEAACENC_NO_ERROR,
          "set the xHE-AAC object type");
    check(IIS_xHEAACEnc_Config_AddParamValueInt(cfg, IIS_XHEAACENC_PARAMETER_TRANSPORTFORMAT,
                                                IIS_XHEAACENC_TRANSPORTFORMAT_LATMLOAS) ==
              IIS_XHEAACENC_NO_ERROR,
          "set the xHE-AAC transport");
    check(IIS_xHEAACEnc_Config_AddParamValueInt(cfg, IIS_XHEAACENC_PARAMETER_STREAMID, 0) ==
              IIS_XHEAACENC_NO_ERROR,
          "set the xHE-AAC stream id");
    check(IIS_xHEAACEnc_Config_AddParamValueInt(cfg, IIS_XHEAACENC_PARAMETER_RAP_OCCURRENCE,
                                                IIS_XHEAACENC_RAP_OCCURRENCE_CONSTANT_INTERVAL) ==
              IIS_XHEAACENC_NO_ERROR,
          "set the xHE-AAC RAP occurrence");
    check(IIS_xHEAACEnc_Config_AddParamValueInt(cfg, IIS_XHEAACENC_PARAMETER_RAP_INTERVAL_MS,
                                                1000) == IIS_XHEAACENC_NO_ERROR,
          "set the xHE-AAC RAP interval");
    check(IIS_xHEAACEnc_Config_AddParamValueFloat(
              cfg, IIS_XHEAACENC_PARAMETER_LIVE_LOUDNESS_LEVEL, -16.0f) == IIS_XHEAACENC_NO_ERROR,
          "set the xHE-AAC live loudness");
    check(IIS_xHEAACEnc_Config_Finalize(cfg) == IIS_XHEAACENC_NO_ERROR,
          "IIS_xHEAACEnc_Config_Finalize");
    check(IIS_xHEAACEnc_Open(&enc, cfg) == IIS_XHEAACENC_NO_ERROR, "IIS_xHEAACEnc_Open");
    check(IIS_xHEAACEnc_GetParam(enc, IIS_XHEAACENC_PARAMETER_FRAMESAMPLES,
                                 IIS_XHEAACENC_PARAM_INT, &frame_size, sizeof frame_size) ==
              IIS_XHEAACENC_NO_ERROR,
          "get the xHE-AAC frame size");
    check(IIS_xHEAACEnc_Config_Delete(cfg) == IIS_XHEAACENC_NO_ERROR,
          "IIS_xHEAACEnc_Config_Delete");

    for (i = 0; i < 10000 && state != IIS_XHEAACENC_ENCODER_STATE_READY_TO_CLOSE; i++) {
        IIS_XHEAACENC_AUINFO au;
        int want = 0, n = 0, out_size = 0;
        unsigned char *out = stream + used;

        check(IIS_xHEAACEnc_GetParam(enc, IIS_XHEAACENC_PARAMETER_SAMPLES_NEXT,
                                     IIS_XHEAACENC_PARAM_INT, &want, sizeof want) ==
                  IIS_XHEAACENC_NO_ERROR,
              "get the samples xHE-AAC needs next");
        if (fed < FRAMES) {
            n = want < (FRAMES - fed) * 2 ? want : (int)((FRAMES - fed) * 2);
        }
        ret = IIS_xHEAACEnc_EncodeFrame(enc, n ? pcm_float + fed * 2 : NULL, n, out, &out_size,
                                        (unsigned)(sizeof stream - used), &au);
        if (ret != IIS_XHEAACENC_NO_ERROR) {
            fprintf(stderr, "IIS_xHEAACEnc_EncodeFrame: %d\n", (int)ret);
            check(0, "IIS_xHEAACEnc_EncodeFrame");
            break;
        }
        fed += n / 2;
        check(IIS_xHEAACEnc_GetEncoderState(enc, &state) == IIS_XHEAACENC_NO_ERROR,
              "IIS_xHEAACEnc_GetEncoderState");
        if (out_size > 0) {
            frames++;
            /* LOAS AudioSyncStream: syncword 0x2B7, then audioMuxLengthBytes. */
            if (out_size < 3 || out[0] != 0x56 || (out[1] & 0xE0) != 0xE0) {
                bad_sync++;
            } else if ((((out[1] & 0x1F) << 8) | out[2]) + 3 != out_size) {
                bad_length++;
            }
            used += out_size;
        }
    }
    printf("xhe-aac: %d frames of %d samples, %ld bytes\n", frames, frame_size, used);
    check(frame_size == 2048, "xHE-AAC frames of 2048 samples");
    check(state == IIS_XHEAACENC_ENCODER_STATE_READY_TO_CLOSE, "the xHE-AAC encoder flushed");
    check(fed == FRAMES, "the xHE-AAC encoder consumed every input sample");
    check(frames > 0 && used > 0, "the xHE-AAC encoder produced LOAS frames");
    check(bad_sync == 0, "every xHE-AAC frame starts with the LOAS syncword 0x2B7");
    check(bad_length == 0, "every LOAS frame length covers the encoder output");
    check(IIS_xHEAACEnc_Delete(enc) == IIS_XHEAACENC_NO_ERROR, "IIS_xHEAACEnc_Delete");

    decoded_frames = decode(TT_MP4_LOAS, used, &info);
    check(decoded_frames == frames, "the decoder decoded every LOAS frame");
    got = (long)decoded_frames * info.frameSize;
    check(info.sampleRate == 44100 && info.numChannels == 2 && info.frameSize == 2048,
          "LOAS stream info: 44100 Hz, 2 channels, 2048 samples a frame");
    check(info.aot == AOT_USAC, "LOAS stream info: USAC");
    check(got >= WIN_FROM + WIN_LEN, "the decoder returned the comparison window");
    if (got >= WIN_FROM + WIN_LEN) {
        /* The live loudness leveler and the decoder's loudness normalization
         * set the level, so compare the channels with each other: the input's
         * left channel carries four times the right channel's energy. */
        double share0 = tone_share(decoded, 0), share1 = tone_share(decoded, 1);
        double balance = mean_square(decoded, 0, 0) / mean_square(decoded, 1, 0);
        printf("xhe-aac: tone share %.5f %.5f, left/right energy %.4f\n", share0, share1,
               balance);
        check(share0 >= 0.95 && share1 >= 0.95, "the xHE-AAC output is the 1 kHz tone");
        check(fabs(balance / 4.0 - 1.0) <= 0.1, "xHE-AAC left/right energy within 10% of 4");
    }
}

int main(void) {
    long n;
    int ch;
    for (n = 0; n < FRAMES; n++) {
        for (ch = 0; ch < 2; ch++) {
            pcm_float[n * 2 + ch] = (float)tone(n, ch);
            pcm[n * 2 + ch] = (INT_PCM)lrint(tone(n, ch) * 32767.0);
        }
    }
    {
        /* A fresh decoder has nothing to decode. */
        HANDLE_AACDECODER dec = aacDecoder_Open(TT_MP4_ADTS, 1);
        check(dec != NULL &&
                  aacDecoder_DecodeFrame(dec, decoded, 4096, 0) == AAC_DEC_NOT_ENOUGH_BITS,
              "an empty decoder reports AAC_DEC_NOT_ENOUGH_BITS");
        aacDecoder_Close(dec);
    }
    check_versions();
    aac_lc_round_trip();
    xhe_round_trip();
    if (failures) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    puts("fdk-aac C consumer: all checks passed");
    return 0;
}
