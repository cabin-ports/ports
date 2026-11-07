// The C++ consumer: <spng.h> through its extern "C" block; an
// Adam7-interlaced gray+alpha PNG built by an independent encoder, decoded
// whole and scanline by scanline; an interlaced image encoded to and decoded
// from stream callbacks, on a caller-supplied allocator that must get every
// block back; truncated streams and the image-size limits.
#include <spng.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <set>
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

struct ctx_free {
    void operator()(spng_ctx *ctx) const { spng_ctx_free(ctx); }
};
using ctx_ptr = std::unique_ptr<spng_ctx, ctx_free>;

// 11x9 gray+alpha 8-bit, Adam7, by an independent encoder (a short Python
// script: the PNG filter and interlace definitions, zlib, CRC-32).  The
// scanlines of all passes, in order, are filtered None, Sub, Up, Average,
// Paeth, None, ...; the zlib stream is split across two IDAT chunks.
// Pixel (x, y) = (gray x*37 + y*11, alpha x*x + y*29 + 7) mod 256.
const unsigned char adam7_png[] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d,
    0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x09,
    0x08, 0x04, 0x00, 0x00, 0x01, 0x39, 0x6a, 0x2e, 0x30, 0x00, 0x00, 0x00,
    0x28, 0x49, 0x44, 0x41, 0x54, 0x78, 0xda, 0x63, 0x60, 0x60, 0xd7, 0x70,
    0x67, 0x8c, 0x78, 0xaf, 0xe1, 0xc0, 0x34, 0x45, 0x9c, 0x79, 0xd1, 0x17,
    0x16, 0x9d, 0xea, 0x29, 0x02, 0x53, 0x0c, 0x18, 0xbc, 0xb8, 0xef, 0x69,
    0x17, 0x65, 0x33, 0x96, 0xd5, 0x4f, 0x51, 0x98, 0xe2, 0xb5, 0x81, 0xb2,
    0x8c, 0x00, 0x00, 0x00, 0x9c, 0x49, 0x44, 0x41, 0x54, 0xc0, 0xa4, 0x53,
    0x02, 0x82, 0xcc, 0x62, 0x8e, 0xa1, 0xaa, 0x55, 0xfa, 0xf3, 0x6d, 0x8f,
    0xf8, 0x67, 0xa6, 0xb2, 0x00, 0x45, 0x58, 0x74, 0x78, 0x74, 0x44, 0xbc,
    0x64, 0x74, 0x54, 0x18, 0x54, 0x39, 0xf2, 0x05, 0x76, 0x2a, 0x30, 0x5b,
    0xf8, 0x46, 0x30, 0x5a, 0x3b, 0x79, 0x71, 0x78, 0x09, 0x78, 0x49, 0x78,
    0x29, 0x30, 0x89, 0x59, 0xc1, 0x20, 0xb3, 0x7d, 0x85, 0x81, 0xa2, 0x81,
    0xea, 0x06, 0x4d, 0x03, 0x5d, 0x16, 0x20, 0x9f, 0x43, 0x0c, 0xa8, 0x40,
    0xcc, 0x8a, 0x81, 0x5b, 0xc5, 0x40, 0x35, 0x54, 0xa3, 0x4a, 0x77, 0xbe,
    0xc9, 0x11, 0xdb, 0x97, 0x1e, 0x7c, 0xa1, 0xc6, 0x29, 0x11, 0xa5, 0xb5,
    0x1d, 0x8c, 0x8a, 0x71, 0xaa, 0x8c, 0xaa, 0xcc, 0xaa, 0xac, 0xaa, 0xec,
    0xaa, 0x9c, 0xaa, 0xdc, 0xaa, 0xbc, 0xaa, 0xfc, 0xaa, 0x82, 0xaa, 0xc2,
    0x48, 0xc6, 0x21, 0x43, 0x66, 0xa3, 0x36, 0x39, 0x39, 0x39, 0x79, 0x39,
    0x05, 0x39, 0x45, 0x39, 0x25, 0x39, 0x65, 0x39, 0x15, 0xb9, 0xa5, 0x72,
    0xcb, 0xe4, 0x96, 0x03, 0x00, 0xfd, 0x0c, 0x2f, 0x25, 0x62, 0xdc, 0xd6,
    0x9e, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60,
    0x82,
};
constexpr std::size_t AW = 11, AH = 9;

void interlaced_reference()
{
    ctx_ptr ctx(spng_ctx_new(0));
    spng_ihdr ihdr;
    std::vector<unsigned char> whole(AW * AH * 4), rows(whole.size());
    bool ok = true;

    spng_set_png_buffer(ctx.get(), adam7_png, sizeof adam7_png);
    check(spng_get_ihdr(ctx.get(), &ihdr) == 0 && ihdr.width == AW && ihdr.height == AH &&
              ihdr.color_type == SPNG_COLOR_TYPE_GRAYSCALE_ALPHA &&
              ihdr.interlace_method == SPNG_INTERLACE_ADAM7,
          "Adam7 IHDR");
    check(spng_decode_image(ctx.get(), whole.data(), whole.size(), SPNG_FMT_RGBA8, 0) == 0,
          "Adam7 decode");
    for (std::size_t y = 0; y < AH; ++y)
        for (std::size_t x = 0; x < AW; ++x) {
            const unsigned char *p = &whole[(y * AW + x) * 4];
            const unsigned g = (x * 37 + y * 11) & 255, a = (x * x + y * 29 + 7) & 255;
            ok = ok && p[0] == g && p[1] == g && p[2] == g && p[3] == a;
        }
    check(ok, "Adam7 pixels");

    // Each scanline is deinterlaced into the full-width buffer of its row.
    ctx_ptr prog(spng_ctx_new(0));
    std::set<int> passes;
    spng_row_info info;
    int scanlines = 0, err;

    spng_set_png_buffer(prog.get(), adam7_png, sizeof adam7_png);
    check(spng_decode_image(prog.get(), nullptr, 0, SPNG_FMT_RGBA8, SPNG_DECODE_PROGRESSIVE) == 0,
          "Adam7 progressive decode");
    do {
        err = spng_get_row_info(prog.get(), &info);
        if (err) break;
        passes.insert(info.pass);
        ++scanlines;
        err = spng_decode_row(prog.get(), &rows[info.row_num * AW * 4], AW * 4);
    } while (!err);
    check(err == SPNG_EOI && passes.size() == 7 && scanlines == 19, "7 passes, 19 scanlines");
    check(rows == whole, "scanline decode matches the whole image");
}

long live_blocks = 0, allocations = 0;

void *SPNG_CDECL counting_malloc(std::size_t size)
{
    void *p = std::malloc(size);
    if (p) ++live_blocks, ++allocations;
    return p;
}
void *SPNG_CDECL counting_realloc(void *old, std::size_t size)
{
    void *p = std::realloc(old, size);
    if (p && !old) ++live_blocks, ++allocations;
    return p;
}
void *SPNG_CDECL counting_calloc(std::size_t count, std::size_t size)
{
    void *p = std::calloc(count, size);
    if (p) ++live_blocks, ++allocations;
    return p;
}
void SPNG_CDECL counting_free(void *p)
{
    if (p) --live_blocks;
    std::free(p);
}

int write_vector(spng_ctx *, void *user, void *src, std::size_t length)
{
    auto *out = static_cast<std::vector<unsigned char> *>(user);
    const auto *bytes = static_cast<const unsigned char *>(src);
    out->insert(out->end(), bytes, bytes + length);
    return 0;
}

struct reader {
    const unsigned char *data;
    std::size_t size, pos, calls;
};

int read_reader(spng_ctx *, void *user, void *dst, std::size_t length)
{
    auto *in = static_cast<reader *>(user);
    ++in->calls;
    if (in->size - in->pos < length) return SPNG_IO_EOF;
    std::memcpy(dst, in->data + in->pos, length);
    in->pos += length;
    return 0;
}

void streams_and_allocator()
{
    spng_alloc alloc = {counting_malloc, counting_realloc, counting_calloc, counting_free};
    const std::size_t w = 13, h = 10;
    std::vector<unsigned char> image(w * h * 3), png, decoded(image.size());
    std::uint32_t seed = 7;
    for (auto &b : image) {
        seed = seed * 1103515245u + 12345u;
        b = static_cast<unsigned char>(seed >> 16);
    }

    {
        ctx_ptr enc(spng_ctx_new2(&alloc, SPNG_CTX_ENCODER));
        spng_ihdr ihdr = {w, h, 8, SPNG_COLOR_TYPE_TRUECOLOR, 0, 0, SPNG_INTERLACE_ADAM7};
        check(enc != nullptr, "encoder on a custom allocator");
        spng_set_png_stream(enc.get(), write_vector, &png);
        spng_set_ihdr(enc.get(), &ihdr);
        check(spng_encode_image(enc.get(), image.data(), image.size(), SPNG_FMT_PNG,
                                SPNG_ENCODE_FINALIZE) == 0,
              "interlaced encode to a stream");
    }
    check(png.size() > 33 && std::memcmp(png.data(), "\x89PNG\r\n\x1a\n", 8) == 0 &&
              png[28] == SPNG_INTERLACE_ADAM7,
          "stream holds an interlaced PNG");

    {
        ctx_ptr dec(spng_ctx_new2(&alloc, 0));
        reader in = {png.data(), png.size(), 0, 0};
        spng_set_png_stream(dec.get(), read_reader, &in);
        check(spng_decode_image(dec.get(), decoded.data(), decoded.size(), SPNG_FMT_RGB8, 0) == 0 &&
                  decoded == image && in.calls > 1,
              "interlaced round trip through streams");
    }

    {
        ctx_ptr dec(spng_ctx_new2(&alloc, 0));
        reader in = {png.data(), png.size() - 20, 0, 0};
        spng_set_png_stream(dec.get(), read_reader, &in);
        check(spng_decode_image(dec.get(), decoded.data(), decoded.size(), SPNG_FMT_RGB8, 0) ==
                  SPNG_IO_EOF,
              "truncated stream");
    }
    check(allocations > 0 && live_blocks == 0, "every block returned to the allocator");
}

void limits()
{
    spng_ihdr ihdr;
    ctx_ptr wide(spng_ctx_new(0)), tall(spng_ctx_new(0));

    spng_set_image_limits(wide.get(), AW - 1, AH);
    spng_set_png_buffer(wide.get(), adam7_png, sizeof adam7_png);
    check(spng_get_ihdr(wide.get(), &ihdr) == SPNG_EUSER_WIDTH, "width limit");
    spng_set_image_limits(tall.get(), AW, AH - 1);
    spng_set_png_buffer(tall.get(), adam7_png, sizeof adam7_png);
    check(spng_get_ihdr(tall.get(), &ihdr) == SPNG_EUSER_HEIGHT, "height limit");
}

} // namespace

int main()
{
    interlaced_reference();
    streams_and_allocator();
    limits();
    if (failures != 0) return 1;
    std::puts("libspng C++ consumer ok");
    return 0;
}
