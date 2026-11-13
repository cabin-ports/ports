// Resizes uniform and gradient RGB images with avir::CImageResizer<> (and its
// SSE variant on x86) and avir::CLancIR, checking sizes and values.
#include "avir.h"
#include "lancir.h"

#if defined(__SSE2__) || defined(_M_X64)
#include "avir_float4_sse.h"
#define TEST_FLOAT4 1
#endif

#include <cstdio>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char *what)
{
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

const int W = 64;
const int H = 48;
const int C = 3;
const int NW = 32;
const int NH = 24;

typedef std::vector<unsigned char> Image;
typedef std::vector<unsigned short> Image16;

// Channel 0 rises 4 per column (0..252), channel 1 rises 4 per row (0..188),
// and channel 2 is 128 throughout.
Image gradient()
{
    Image img(W * H * C);
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            img[(y * W + x) * C] = static_cast<unsigned char>(x * 4);
            img[(y * W + x) * C + 1] = static_cast<unsigned char>(y * 4);
            img[(y * W + x) * C + 2] = 128;
        }
    }
    return img;
}

int distance(int a, int b) { return a > b ? a - b : b - a; }

template <class T> bool near_value(const std::vector<T> &img, int value)
{
    for (std::size_t i = 0; i < img.size(); ++i) {
        if (distance(img[i], value) > 1) {
            return false;
        }
    }
    return true;
}

// Halving centres output pixel i on input i * 2 + 0.5, whose value is
// i * 8 + 2.  Border pixels depend on each resizer's edge handling, so only
// interior ones are held to that value; every pixel must keep the order.
bool halved_gradient(const Image &img)
{
    for (int y = 0; y < NH; ++y) {
        for (int x = 0; x < NW; ++x) {
            const unsigned char *p = &img[(y * NW + x) * C];
            if (distance(p[2], 128) > 1 || (x > 0 && x < NW - 1 && distance(p[0], x * 8 + 2) > 1) ||
                (y > 0 && y < NH - 1 && distance(p[1], y * 8 + 2) > 1) || (x > 0 && p[0] <= p[-C]) ||
                (y > 0 && p[1] <= p[1 - NW * C])) {
                return false;
            }
        }
    }
    return true;
}

template <class Resizer> void test_avir(const Resizer &resizer, const char *name)
{
    std::fprintf(stderr, "checking %s\n", name);
    const Image flat(W * H * C, 128);

    Image down(NW * NH * C, 0);
    resizer.resizeImage(&flat[0], W, H, 0, &down[0], NW, NH, C, 0);
    check(near_value(down, 128), "a downsized uniform image keeps its value");

    Image up(2 * W * 2 * H * C, 0);
    resizer.resizeImage(&flat[0], W, H, 0, &up[0], 2 * W, 2 * H, C, 0);
    check(near_value(up, 128), "an upsized uniform image keeps its value");

    Image square(NW * NW * C, 0);
    resizer.resizeImage(&flat[0], W, H, 0, &square[0], NW, NW, C, 0);
    check(near_value(square, 128), "a non-proportional resize keeps a uniform value");

    const Image ramp = gradient();
    Image halved(NW * NH * C, 0);
    resizer.resizeImage(&ramp[0], W, H, 0, &halved[0], NW, NH, C, 0);
    check(halved_gradient(halved), "a halved gradient keeps its values and order");

    // 8-bit input to float output rescales 0..255 to 0..1.
    std::vector<float> scaled(NW * NH * C, 0.0f);
    resizer.resizeImage(&flat[0], W, H, 0, &scaled[0], NW, NH, C, 0);
    bool ok = true;
    for (std::size_t i = 0; i < scaled.size(); ++i) {
        const float d = scaled[i] - 128.0f / 255.0f;
        ok = ok && d < 1e-4f && d > -1e-4f;
    }
    check(ok, "float output is rescaled to 0..1");
}

void test_avir_16()
{
    std::fprintf(stderr, "checking avir::CImageResizer<> at 16 bits\n");
    const avir::CImageResizer<> resizer(16);
    const Image16 flat(W * H * C, 40000);
    Image16 down(NW * NH * C, 0);
    resizer.resizeImage(&flat[0], W, H, 0, &down[0], NW, NH, C, 0);
    check(near_value(down, 40000), "a 16-bit uniform image keeps its value");
}

void test_lancir()
{
    std::fprintf(stderr, "checking avir::CLancIR\n");
    avir::CLancIR resizer;
    const Image flat(W * H * C, 128);

    Image down(NW * NH * C, 0);
    check(resizer.resizeImage(&flat[0], W, H, &down[0], NW, NH, C) == NH, "LANCIR returns the output height");
    check(near_value(down, 128), "a downsized uniform image keeps its value");

    Image up(2 * W * 2 * H * C, 0);
    check(resizer.resizeImage(&flat[0], W, H, &up[0], 2 * W, 2 * H, C) == 2 * H, "LANCIR upsizes");
    check(near_value(up, 128), "an upsized uniform image keeps its value");

    Image square(NW * NW * C, 0);
    check(resizer.resizeImage(&flat[0], W, H, &square[0], NW, NW, C) == NW && near_value(square, 128),
          "a non-proportional resize keeps a uniform value");

    const Image ramp = gradient();
    Image halved(NW * NH * C, 0);
    check(resizer.resizeImage(&ramp[0], W, H, &halved[0], NW, NH, C) == NH, "LANCIR resizes a gradient");
    check(halved_gradient(halved), "a halved gradient keeps its values and order");

    const Image16 flat16(W * H * C, 40000);
    Image16 down16(NW * NH * C, 0);
    check(resizer.resizeImage(&flat16[0], W, H, &down16[0], NW, NH, C) == NH && near_value(down16, 40000),
          "a 16-bit uniform image keeps its value");

    check(resizer.resizeImage(&flat[0], W, H, &down[0], 0, NH, C) == 0, "LANCIR rejects a zero width");
}

} // namespace

int main()
{
    // Named objects: the resizers are not copyable, which C++98 requires for
    // binding a temporary to a reference.
    const avir::CImageResizer<> plain(8);
    test_avir(plain, "avir::CImageResizer<>");
#if defined(TEST_FLOAT4)
    const avir::CImageResizer<avir::fpclass_float4> simd(8);
    test_avir(simd, "avir::CImageResizer<avir::fpclass_float4>");
#endif
    test_avir_16();
    test_lancir();
    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::puts("avir 3.1.0 ok");
    return 0;
}
