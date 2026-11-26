/* Encodes a stereo triangle wave at 44.1 kHz as AAC-LC and as HE-AAC v1
 * ADTS and checks every frame header against ISO/IEC 14496-3 1.A.2.2:
 * syncword, MPEG-4 ID, layer, profile, sampling frequency index, channel
 * configuration and frame length. */
#include <faac.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum { RATE = 44100, CHANNELS = 2, FRAMES = 8 };

static int16_t pcm[2048 * CHANNELS + 2];
static uint8_t out[1 << 16];
static int failures;

static void check(int ok, const char *what)
{
    if (!ok) {
        fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

/* A 441 Hz triangle, inverted and halved on the right channel. */
static void fill(uint32_t first, uint32_t samples)
{
    for (uint32_t i = 0; i < samples; i++) {
        int32_t p = (int32_t)((first + i) % 100);
        int32_t v = ((p < 50 ? p : 100 - p) - 25) * 320;
        pcm[2 * i] = (int16_t)v;
        pcm[2 * i + 1] = (int16_t)(-v / 2);
    }
}

/* AOT 2 (LC) is profile 1; 44100 Hz is index 4, 22050 Hz index 7; stereo is
 * channel configuration 2. */
static void check_adts(const uint8_t *f, uint32_t size, unsigned sf_index)
{
    check(size >= 7, "ADTS frame holds its header");
    check(f[0] == 0xFF && (f[1] & 0xF0) == 0xF0, "ADTS syncword 0xFFF");
    check((f[1] & 0x0E) == 0, "ADTS ID 0 (MPEG-4) and layer 0");
    check(f[2] >> 6 == 1, "ADTS profile LC");
    check(((f[2] >> 2) & 0x0F) == sf_index, "ADTS sampling frequency index");
    check((((f[2] & 1) << 2) | (f[3] >> 6)) == 2, "ADTS channel configuration 2");
    check((((uint32_t)(f[3] & 3) << 11) | ((uint32_t)f[4] << 3) | (f[5] >> 5)) == size,
          "ADTS frame length is the bytes written");
    check((f[6] & 3) == 0, "one raw data block per ADTS frame");
}

/* Encodes FRAMES frames, then flushes until a call writes nothing. */
static void encode(faac_encoder *enc, const faac_encoder_info *info, unsigned sf_index)
{
    uint32_t frames = 0, written;
    int flushed = 0;

    check(info->max_output_bytes <= sizeof out, "output buffer bound fits");
    for (uint32_t i = 0; i < FRAMES + 16 && !flushed; i++) {
        uint32_t n = i < FRAMES ? info->frame_samples : 0;
        fill(i * info->frame_samples, n);
        if (faac_encoder_encode(enc, pcm, n * CHANNELS, out, info->max_output_bytes, &written)
            != FAAC_OK) {
            check(0, "faac_encoder_encode");
            return;
        }
        check(written <= info->max_output_bytes, "frame within max_output_bytes");
        if (written > 0) {
            check_adts(out, written, sf_index);
            frames++;
        } else if (i >= FRAMES) {
            flushed = 1;
        }
    }
    check(flushed, "flush ends with an empty call");
    /* A decoder drops encoder_delay samples, so the frames span them as well
     * as the input. */
    check((uint64_t)frames * info->frame_samples
              >= (uint64_t)FRAMES * info->frame_samples + info->encoder_delay,
          "frames cover the input and the priming delay");
}

int main(void)
{
    faac_library_info lib;
    faac_params p;
    faac_encoder_info info;
    faac_encoder *enc = NULL;
    uint32_t written;

    memset(&lib, 0, sizeof lib);
    lib.struct_size = sizeof lib;
    check(faac_get_library_info(&lib) == FAAC_OK, "faac_get_library_info");
    check(lib.version != NULL && strcmp(lib.version, "3.0.1") == 0, "version 3.0.1");
    /* meson_options.txt defaults: 8 channels, full SBR analysis, SBR and DRC
     * on, Parametric Stereo off. */
    check(lib.max_channels == 8, "max-channels 8");
    check(lib.sbr_decimation == 1, "sbr-decimation 1");
    check(lib.flags == (FAAC_LIB_SBR | FAAC_LIB_DRC), "SBR and DRC, no PS");

    check(faac_params_init(&p, 4) == FAAC_ERR_INVALID_ARGUMENT, "params_init rejects a short struct");
    check(faac_params_init(&p, sizeof p) == FAAC_OK, "faac_params_init");
    check(p.struct_size == sizeof p, "params struct_size");

    p.sample_rate = RATE;
    p.channels = CHANNELS;
    p.object_type = FAAC_OBJ_HE_AAC_V2;
    check(faac_encoder_open(&p, &enc) == FAAC_ERR_UNSUPPORTED && enc == NULL,
          "HE-AAC v2 is left out of the build");

    p.object_type = FAAC_OBJ_LC;
    p.bit_rate = 128000;
    p.stream_format = FAAC_STREAM_ADTS;
    p.input_format = FAAC_INPUT_16BIT;
    p.drc_profile = FAAC_DRC_MUSIC;
    p.drc_ref_level = -20.0f;
    check(faac_encoder_open(&p, &enc) == FAAC_OK && enc != NULL, "open AAC-LC");
    if (enc == NULL)
        return 1;
    memset(&info, 0, sizeof info);
    info.struct_size = sizeof info;
    check(faac_encoder_get_info(enc, &info) == FAAC_OK, "get_info AAC-LC");
    check(info.frame_samples == 1024, "LC frame of 1024 samples");
    check(info.object_type == FAAC_OBJ_LC, "object type LC");
    check(info.stream_format == FAAC_STREAM_ADTS, "stream format ADTS");
    check(info.bit_rate == 128000, "bit rate 128000");
    check(info.drc_profile == FAAC_DRC_MUSIC, "DRC profile music");
    check(info.drc_ref_level == -20.0f, "DRC reference level");
    /* AudioSpecificConfig: AOT 2, frequency index 4, channel configuration 2,
     * then three zero GASpecificConfig flags. */
    check(info.asc_size == 2 && info.asc[0] == 0x12 && info.asc[1] == 0x10, "LC AudioSpecificConfig");
    check(faac_encoder_encode(enc, pcm, 1024 * CHANNELS, out, 16, &written)
              == FAAC_ERR_OUTPUT_TOO_SMALL,
          "encode rejects a small output buffer");
    check(faac_encoder_encode(enc, pcm, 1024 * CHANNELS + 2, out, sizeof out, &written)
              == FAAC_ERR_INPUT_OVERFLOW,
          "encode rejects more than a frame of input");
    encode(enc, &info, 4);
    check(faac_encoder_close(&enc) == FAAC_OK && enc == NULL, "close AAC-LC");

    /* HE-AAC v1 signals SBR implicitly: ADTS carries the LC core at half the
     * input rate. */
    p.object_type = FAAC_OBJ_HE_AAC_V1;
    p.bit_rate = 64000;
    p.drc_profile = FAAC_DRC_OFF;
    check(faac_encoder_open(&p, &enc) == FAAC_OK && enc != NULL, "open HE-AAC v1");
    if (enc == NULL)
        return 1;
    memset(&info, 0, sizeof info);
    info.struct_size = sizeof info;
    check(faac_encoder_get_info(enc, &info) == FAAC_OK, "get_info HE-AAC v1");
    check(info.frame_samples == 2048, "HE-AAC frame of 2048 samples");
    check(info.object_type == FAAC_OBJ_HE_AAC_V1, "object type HE-AAC v1");
    check(info.bit_rate == 64000, "bit rate 64000");
    encode(enc, &info, 7);
    check(faac_encoder_close(&enc) == FAAC_OK && enc == NULL, "close HE-AAC v1");

    if (failures != 0)
        return 1;
    puts("faac C consumer: ok");
    return 0;
}
