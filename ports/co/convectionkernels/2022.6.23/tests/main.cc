#include <ConvectionKernels.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static int failures = 0;

static void check(bool ok, const char *what, int value)
{
    if (!ok)
    {
        printf("FAIL: %s (%d)\n", what, value);
        failures++;
    }
}

int main()
{
    const unsigned n = cvtt::NumParallelBlocks;
    cvtt::Options options;

    // BC7: 8 blocks, one RGBA gradient each, round-trip through the library's decoder.
    cvtt::PixelBlockU8 in[cvtt::NumParallelBlocks];
    for (unsigned b = 0; b < n; b++)
        for (int p = 0; p < 16; p++)
        {
            in[b].m_pixels[p][0] = static_cast<uint8_t>(b * 30 + (p % 4) * 6);
            in[b].m_pixels[p][1] = static_cast<uint8_t>(255 - b * 20 - (p / 4) * 6);
            in[b].m_pixels[p][2] = static_cast<uint8_t>(40 + b * 10);
            in[b].m_pixels[p][3] = static_cast<uint8_t>(255 - p * 3);
        }

    cvtt::BC7EncodingPlan plan;
    uint8_t bc7[16 * cvtt::NumParallelBlocks];
    cvtt::Kernels::EncodeBC7(bc7, in, options, plan);

    cvtt::PixelBlockU8 out[cvtt::NumParallelBlocks];
    cvtt::Kernels::DecodeBC7(out, bc7);
    int worst = 0;
    for (unsigned b = 0; b < n; b++)
        for (int p = 0; p < 16; p++)
            for (int c = 0; c < 4; c++)
            {
                int d = abs(int(out[b].m_pixels[p][c]) - int(in[b].m_pixels[p][c]));
                if (d > worst)
                    worst = d;
            }
    printf("bc7 worst error %d\n", worst);
    check(worst <= 12, "bc7 round trip within tolerance", worst);

    // BC7: a solid-colour block (all eight identical) decodes to that colour.
    for (unsigned b = 0; b < n; b++)
        for (int p = 0; p < 16; p++)
        {
            in[b].m_pixels[p][0] = 200;
            in[b].m_pixels[p][1] = 100;
            in[b].m_pixels[p][2] = 50;
            in[b].m_pixels[p][3] = 255;
        }
    cvtt::Kernels::EncodeBC7(bc7, in, options, plan);
    cvtt::Kernels::DecodeBC7(out, bc7);
    worst = 0;
    for (unsigned b = 0; b < n; b++)
        for (int p = 0; p < 16; p++)
            for (int c = 0; c < 4; c++)
            {
                int d = abs(int(out[b].m_pixels[p][c]) - int(in[b].m_pixels[p][c]));
                if (d > worst)
                    worst = d;
            }
    printf("bc7 solid worst error %d\n", worst);
    check(worst <= 2, "bc7 solid colour", worst);

    // BC1: a red/blue checker is exactly representable in RGB565, so it decodes exactly.
    for (unsigned b = 0; b < n; b++)
        for (int p = 0; p < 16; p++)
        {
            bool red = ((p % 4) + (p / 4)) % 2 == 0;
            in[b].m_pixels[p][0] = red ? 255 : 0;
            in[b].m_pixels[p][1] = 0;
            in[b].m_pixels[p][2] = red ? 0 : 255;
            in[b].m_pixels[p][3] = 255;
        }
    uint8_t bc1[8 * cvtt::NumParallelBlocks];
    cvtt::Kernels::EncodeBC1(bc1, in, options);
    int bad = 0;
    for (unsigned b = 0; b < n; b++)
    {
        const uint8_t *blk = bc1 + b * 8;
        unsigned c0 = blk[0] | (blk[1] << 8), c1 = blk[2] | (blk[3] << 8);
        int pal[4][3];
        unsigned ends[2] = {c0, c1};
        for (int e = 0; e < 2; e++)
        {
            pal[e][0] = ((ends[e] >> 11) & 31) * 255 / 31;
            pal[e][1] = ((ends[e] >> 5) & 63) * 255 / 63;
            pal[e][2] = (ends[e] & 31) * 255 / 31;
        }
        for (int c = 0; c < 3; c++)
        {
            if (c0 > c1)
            {
                pal[2][c] = (2 * pal[0][c] + pal[1][c]) / 3;
                pal[3][c] = (pal[0][c] + 2 * pal[1][c]) / 3;
            }
            else
            {
                pal[2][c] = (pal[0][c] + pal[1][c]) / 2;
                pal[3][c] = 0;
            }
        }
        for (int p = 0; p < 16; p++)
        {
            int idx = (blk[4 + p / 4] >> ((p % 4) * 2)) & 3;
            for (int c = 0; c < 3; c++)
                if (pal[idx][c] != in[b].m_pixels[p][c])
                    bad++;
        }
    }
    check(bad == 0, "bc1 checker decodes exactly", bad);

    // BC6H: a solid HDR block (half floats 1.0, 0.5, 0.25) round-trips through the decoder.
    cvtt::PixelBlockF16 hin[cvtt::NumParallelBlocks], hout[cvtt::NumParallelBlocks];
    for (unsigned b = 0; b < n; b++)
        for (int p = 0; p < 16; p++)
        {
            hin[b].m_pixels[p][0] = 0x3C00;
            hin[b].m_pixels[p][1] = 0x3800;
            hin[b].m_pixels[p][2] = 0x3400;
            hin[b].m_pixels[p][3] = 0x3C00;
        }
    uint8_t bc6[16 * cvtt::NumParallelBlocks];
    cvtt::Kernels::EncodeBC6HU(bc6, hin, options);
    cvtt::Kernels::DecodeBC6HU(hout, bc6);
    worst = 0;
    for (unsigned b = 0; b < n; b++)
        for (int p = 0; p < 16; p++)
            for (int c = 0; c < 3; c++)
            {
                int d = abs(int(hout[b].m_pixels[p][c]) - int(hin[b].m_pixels[p][c]));
                if (d > worst)
                    worst = d;
            }
    printf("bc6h worst error %d\n", worst);
    check(worst <= 16, "bc6h solid round trip", worst);

    if (failures == 0)
        printf("ok\n");
    return failures == 0 ? 0 : 1;
}
