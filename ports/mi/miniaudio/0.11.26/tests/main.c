#include <miniaudio.h>

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define FRAMES 128

static int failures;

static void check(int ok, const char *what)
{
    if (!ok) {
        fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

/* An in-memory WAV file for ma_encoder. */
typedef struct {
    unsigned char data[1024];
    size_t size;
    size_t cursor;
} memory_file;

static ma_result memory_write(ma_encoder *encoder, const void *buffer, size_t bytes, size_t *written)
{
    memory_file *file = (memory_file *)encoder->pUserData;
    if (bytes > sizeof(file->data) - file->cursor) {
        return MA_OUT_OF_MEMORY;
    }
    memcpy(file->data + file->cursor, buffer, bytes);
    file->cursor += bytes;
    if (file->cursor > file->size) {
        file->size = file->cursor;
    }
    *written = bytes;
    return MA_SUCCESS;
}

static ma_result memory_seek(ma_encoder *encoder, ma_int64 offset, ma_seek_origin origin)
{
    memory_file *file = (memory_file *)encoder->pUserData;
    ma_int64 base = 0;
    if (origin == ma_seek_origin_current) {
        base = (ma_int64)file->cursor;
    } else if (origin == ma_seek_origin_end) {
        base = (ma_int64)file->size;
    }
    if (base + offset < 0 || base + offset > (ma_int64)sizeof(file->data)) {
        return MA_INVALID_ARGS;
    }
    file->cursor = (size_t)(base + offset);
    return MA_SUCCESS;
}

/* 750 Hz at 48 kHz advances the waveform by exactly 1/64 per frame. */
static void test_waveforms(ma_int16 *square)
{
    ma_waveform_config config;
    ma_waveform waveform;
    ma_uint64 read = 0;
    float sine[FRAMES];
    int i;
    int square_ok = 1;

    config = ma_waveform_config_init(ma_format_f32, 1, 48000, ma_waveform_type_sine, 0.5, 750);
    check(ma_waveform_init(&config, &waveform) == MA_SUCCESS, "sine waveform init");
    check(ma_waveform_read_pcm_frames(&waveform, sine, FRAMES, &read) == MA_SUCCESS && read == FRAMES,
          "sine waveform read");
    check(fabs(sine[0]) < 1e-6 && fabs(sine[16] - 0.5) < 1e-6 && fabs(sine[48] + 0.5) < 1e-6 &&
              fabs(sine[8] - 0.5 * sin(3.14159265358979 / 4)) < 1e-6,
          "sine samples follow 0.5 * sin(2 pi n / 64)");
    ma_waveform_uninit(&waveform);

    config = ma_waveform_config_init(ma_format_s16, 1, 48000, ma_waveform_type_square, 0.5, 750);
    check(ma_waveform_init(&config, &waveform) == MA_SUCCESS, "square waveform init");
    check(ma_waveform_read_pcm_frames(&waveform, square, FRAMES, &read) == MA_SUCCESS && read == FRAMES,
          "square waveform read");
    for (i = 0; i < FRAMES; ++i) {
        square_ok &= square[i] == ((i % 64) < 32 ? 16383 : -16383);
    }
    check(square_ok, "square samples are +/-0.5 * 32767 with a 50% duty cycle");
    ma_waveform_uninit(&waveform);
}

static void test_wav_round_trip(const ma_int16 *square)
{
    static memory_file file;
    ma_encoder_config encoder_config;
    ma_encoder encoder;
    ma_decoder_config decoder_config;
    ma_decoder decoder;
    ma_uint64 frames = 0;
    ma_format format;
    ma_uint32 channels = 0;
    ma_uint32 sample_rate = 0;
    ma_int16 pcm[FRAMES];
    ma_uint64 length = 0;
    float stereo[FRAMES * 2];
    int i;
    int stereo_ok = 1;

    encoder_config = ma_encoder_config_init(ma_encoding_format_wav, ma_format_s16, 1, 48000);
    check(ma_encoder_init(memory_write, memory_seek, &file, &encoder_config, &encoder) == MA_SUCCESS,
          "WAV encoder init");
    check(ma_encoder_write_pcm_frames(&encoder, square, FRAMES, &frames) == MA_SUCCESS && frames == FRAMES,
          "WAV encoder writes every frame");
    ma_encoder_uninit(&encoder);
    check(file.size == 44 + FRAMES * 2 && memcmp(file.data, "RIFF", 4) == 0 && memcmp(file.data + 8, "WAVE", 4) == 0,
          "the encoder wrote a 44-byte RIFF/WAVE header and the samples");

    decoder_config = ma_decoder_config_init(ma_format_unknown, 0, 0);
    check(ma_decoder_init_memory(file.data, file.size, &decoder_config, &decoder) == MA_SUCCESS, "WAV decoder init");
    check(ma_decoder_get_data_format(&decoder, &format, &channels, &sample_rate, NULL, 0) == MA_SUCCESS &&
              format == ma_format_s16 && channels == 1 && sample_rate == 48000,
          "the decoder reports the encoded format");
    check(ma_decoder_get_length_in_pcm_frames(&decoder, &frames) == MA_SUCCESS && frames == FRAMES,
          "the decoder reports the frame count");
    check(ma_decoder_read_pcm_frames(&decoder, pcm, FRAMES, &frames) == MA_SUCCESS && frames == FRAMES &&
              memcmp(pcm, square, sizeof(pcm)) == 0,
          "decoded samples equal the encoded ones");
    ma_decoder_uninit(&decoder);

    /* Mono s16 to stereo f32: the decoder's converter copies the channel and
       scales by 1/32768. */
    decoder_config = ma_decoder_config_init(ma_format_f32, 2, 48000);
    check(ma_decoder_init_memory(file.data, file.size, &decoder_config, &decoder) == MA_SUCCESS,
          "converting WAV decoder init");
    check(ma_decoder_read_pcm_frames(&decoder, stereo, FRAMES, &frames) == MA_SUCCESS && frames == FRAMES,
          "converting decoder reads every frame");
    for (i = 0; i < FRAMES; ++i) {
        stereo_ok &= stereo[i * 2] == square[i] / 32768.0f && stereo[i * 2 + 1] == stereo[i * 2];
    }
    check(stereo_ok, "converted frames carry the sample on both channels");
    ma_decoder_uninit(&decoder);

    decoder_config = ma_decoder_config_init(ma_format_s16, 1, 24000);
    check(ma_decoder_init_memory(file.data, file.size, &decoder_config, &decoder) == MA_SUCCESS,
          "resampling WAV decoder init");
    /* The linear resampler may count one frame past the halfway point. */
    check(ma_decoder_get_length_in_pcm_frames(&decoder, &length) == MA_SUCCESS &&
              (length == FRAMES / 2 || length == FRAMES / 2 + 1),
          "resampling to 24 kHz about halves the frame count");
    check(ma_decoder_read_pcm_frames(&decoder, pcm, FRAMES, &frames) == MA_SUCCESS && frames > 56 && frames <= length,
          "the resampling decoder reads about half the frames");
    /* At 24 kHz the square wave's half period is 16 frames; the middle of each
       half keeps its sign through the resampler's low-pass filter. */
    check(pcm[8] > 0 && pcm[24] < 0 && pcm[40] > 0 && pcm[56] < 0, "the resampled square wave keeps its shape");
    ma_decoder_uninit(&decoder);
}

/* The null backend runs a playback device on a timer, with no audio
   hardware. */
typedef struct {
    ma_mutex lock;
    ma_uint64 frames;
} playback_state;

static void playback_callback(ma_device *device, void *output, const void *input, ma_uint32 frame_count)
{
    playback_state *state = (playback_state *)device->pUserData;
    (void)input;
    memset(output, 0, frame_count * ma_get_bytes_per_frame(device->playback.format, device->playback.channels));
    ma_mutex_lock(&state->lock);
    state->frames += frame_count;
    ma_mutex_unlock(&state->lock);
}

static ma_uint64 played_frames(playback_state *state)
{
    ma_uint64 frames;
    ma_mutex_lock(&state->lock);
    frames = state->frames;
    ma_mutex_unlock(&state->lock);
    return frames;
}

static void test_null_device(void)
{
    ma_backend backends[1];
    ma_context context;
    ma_device_config config;
    ma_device device;
    static playback_state state;
    clock_t start;
    clock_t now;

    backends[0] = ma_backend_null;
    check(strcmp(ma_get_backend_name(ma_backend_null), "Null") == 0, "the null backend's name");
    if (ma_context_init(backends, 1, NULL, &context) != MA_SUCCESS) {
        check(0, "null context init");
        return;
    }
    check(context.backend == ma_backend_null, "the context uses the null backend");
    if (ma_mutex_init(&state.lock) != MA_SUCCESS) {
        check(0, "mutex init");
        ma_context_uninit(&context);
        return;
    }

    config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_f32;
    config.playback.channels = 2;
    config.sampleRate = 48000;
    config.dataCallback = playback_callback;
    config.pUserData = &state;
    if (ma_device_init(&context, &config, &device) != MA_SUCCESS) {
        check(0, "null playback device init");
    } else {
        check(device.playback.format == ma_format_f32 && device.playback.channels == 2 && device.sampleRate == 48000,
              "the device keeps the requested format");
        if (ma_device_start(&device) != MA_SUCCESS) {
            check(0, "device start");
        } else {
            /* Wait for 100 ms of frames, failing rather than hanging after
               about ten seconds of this spinning thread's processor time. */
            start = clock();
            while (played_frames(&state) < 4800 && start != (clock_t)-1 && (now = clock()) != (clock_t)-1 &&
                   now - start < 10 * CLOCKS_PER_SEC) {
            }
            check(played_frames(&state) >= 4800, "the callback was asked for at least 100 ms of frames");
            check(ma_device_stop(&device) == MA_SUCCESS && !ma_device_is_started(&device), "device stop");
        }
        ma_device_uninit(&device);
    }
    ma_mutex_uninit(&state.lock);
    ma_context_uninit(&context);
}

int main(void)
{
    static ma_int16 square[FRAMES];

    check(strcmp(ma_version_string(), "0.11.26") == 0, "ma_version_string");
    test_waveforms(square);
    test_wav_round_trip(square);
    test_null_device();
    if (failures != 0) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    puts("miniaudio 0.11.26 C ok");
    return 0;
}
