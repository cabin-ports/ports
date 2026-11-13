#include <miniaudio.h>

#include <cstdio>
#include <cstring>

namespace {

int failures = 0;

void check(bool ok, const char *what)
{
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

void test_ring_buffer()
{
    ma_pcm_rb rb;
    check(ma_pcm_rb_init(ma_format_s16, 2, 64, NULL, NULL, &rb) == MA_SUCCESS, "ring buffer init");

    ma_uint32 frames = 48;
    void *buffer = NULL;
    check(ma_pcm_rb_acquire_write(&rb, &frames, &buffer) == MA_SUCCESS && frames == 48, "acquire 48 frames to write");
    ma_int16 *out = static_cast<ma_int16 *>(buffer);
    for (int i = 0; i < 48 * 2; ++i) {
        out[i] = static_cast<ma_int16>(i * 100 - 4000);
    }
    check(ma_pcm_rb_commit_write(&rb, frames) == MA_SUCCESS, "commit the write");
    check(ma_pcm_rb_available_read(&rb) == 48 && ma_pcm_rb_available_write(&rb) == 16,
          "48 frames to read, 16 to write");

    frames = 64;
    check(ma_pcm_rb_acquire_read(&rb, &frames, &buffer) == MA_SUCCESS && frames == 48,
          "a read is capped at what was written");
    const ma_int16 *in = static_cast<const ma_int16 *>(buffer);
    bool same = true;
    for (int i = 0; i < 48 * 2; ++i) {
        same = same && in[i] == i * 100 - 4000;
    }
    check(same, "the frames read are the frames written");
    check(ma_pcm_rb_commit_read(&rb, frames) == MA_SUCCESS && ma_pcm_rb_available_read(&rb) == 0,
          "commit the read");
    ma_pcm_rb_uninit(&rb);
}

void test_channel_converter()
{
    ma_channel_converter_config config =
        ma_channel_converter_config_init(ma_format_f32, 2, NULL, 1, NULL, ma_channel_mix_mode_default);
    ma_channel_converter converter;
    check(ma_channel_converter_init(&config, NULL, &converter) == MA_SUCCESS, "channel converter init");
    const float stereo[] = {0.25f, 0.75f, -1.0f, 0.5f, 0.0f, 0.0f};
    float mono[3] = {1.0f, 1.0f, 1.0f};
    check(ma_channel_converter_process_pcm_frames(&converter, mono, stereo, 3) == MA_SUCCESS, "stereo to mono");
    check(mono[0] == 0.5f && mono[1] == -0.25f && mono[2] == 0.0f, "stereo to mono averages the channels");
    ma_channel_converter_uninit(&converter, NULL);
}

} // namespace

int main()
{
    check(MA_VERSION_MAJOR == 0 && MA_VERSION_MINOR == 11 && MA_VERSION_REVISION == 26 &&
              std::strcmp(ma_version_string(), MA_VERSION_STRING) == 0,
          "version macros match ma_version_string()");
    test_ring_buffer();
    test_channel_converter();
    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::puts("miniaudio 0.11.26 C++ ok");
    return 0;
}
