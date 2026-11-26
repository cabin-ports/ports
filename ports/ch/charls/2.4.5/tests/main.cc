#include <charls/charls.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

static bool ok = true;

static void check(bool cond, const char* what)
{
    if (!cond)
    {
        std::printf("FAIL: %s\n", what);
        ok = false;
    }
}

int main()
{
    const uint32_t width = 64;
    const uint32_t height = 64;
    std::vector<uint8_t> source(width * height);
    for (uint32_t y = 0; y < height; ++y)
        for (uint32_t x = 0; x < width; ++x)
            source[y * width + x] = static_cast<uint8_t>(x * 3 + y * 2 + ((x * y) % 5));

    const charls::frame_info frame{width, height, 8, 1};

    check(std::string(charls_get_version_string()) == "2.4.5", "version string");

    try
    {
        const auto encoded = charls::jpegls_encoder::encode(source, frame);
        check(encoded.size() > 0 && encoded.size() < source.size(), "lossless stream is compressed");

        std::vector<uint8_t> decoded;
        const auto result = charls::jpegls_decoder::decode(encoded, decoded);
        check(result.first.width == width, "width");
        check(result.first.height == height, "height");
        check(result.first.bits_per_sample == 8, "bits per sample");
        check(result.first.component_count == 1, "component count");
        check(result.second == charls::interleave_mode::none, "interleave mode");
        check(decoded == source, "lossless round trip");

        charls::jpegls_encoder encoder;
        encoder.frame_info(frame).near_lossless(2);
        std::vector<uint8_t> lossy(encoder.estimated_destination_size());
        encoder.destination(lossy);
        lossy.resize(encoder.encode(source));

        charls::jpegls_decoder decoder{lossy, true};
        check(decoder.near_lossless() == 2, "near-lossless parameter");
        const auto approx = decoder.decode<std::vector<uint8_t>>();
        check(approx.size() == source.size(), "near-lossless size");
        for (size_t i = 0; i < approx.size() && i < source.size(); ++i)
            if (std::abs(static_cast<int>(approx[i]) - static_cast<int>(source[i])) > 2)
            {
                check(false, "near-lossless sample within 2");
                break;
            }

        std::vector<uint8_t> garbage(32, 0x55);
        bool threw = false;
        try
        {
            const charls::jpegls_decoder bad{garbage, true};
            (void)bad;
        }
        catch (const charls::jpegls_error&)
        {
            threw = true;
        }
        check(threw, "invalid stream is rejected");
    }
    catch (const std::exception& e)
    {
        std::printf("FAIL: exception: %s\n", e.what());
        ok = false;
    }
    return ok ? 0 : 1;
}
