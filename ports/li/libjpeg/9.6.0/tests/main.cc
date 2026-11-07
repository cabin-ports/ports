// The C++ consumer: libjpeg 9f's lossless mode (an RGB file with the
// reversible color transform and 1x1 DCT blocks, cjpeg -rgb1 -block 1),
// grayscale files with comment and application markers, scaled decoding,
// two-pass color quantization, and buffered-image decoding of a
// progressive file.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <jpeglib.h>

static bool ok = true;

static void check(bool cond, const char *what) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ok = false;
    }
}

using Bytes = std::vector<unsigned char>;

struct Image {
    JDIMENSION width;
    JDIMENSION height;
    int components;
    Bytes pixels;
};

// A deterministic image that is not smooth, so that only a lossless
// encoder reproduces it.
static Image noise(JDIMENSION width, JDIMENSION height, int components) {
    Image image{width, height, components, Bytes(std::size_t(width) * height * components)};
    unsigned long state = 12345;
    for (unsigned char &p : image.pixels) {
        state = (state * 1103515245UL + 12345UL) & 0x7fffffffUL;
        p = static_cast<unsigned char>(state >> 16);
    }
    return image;
}

// A grayscale ramp: 4 * x + 2 * y.
static Image ramp(JDIMENSION width, JDIMENSION height) {
    Image image{width, height, 1, Bytes(std::size_t(width) * height)};
    for (JDIMENSION y = 0; y < height; ++y)
        for (JDIMENSION x = 0; x < width; ++x)
            image.pixels[std::size_t(y) * width + x] = static_cast<unsigned char>(4 * x + 2 * y);
    return image;
}

struct Options {
    bool lossless = false;
    bool progressive = false;
    int quality = 75;
    std::string comment;
    Bytes app2;
};

static Bytes encode(const Image &image, const Options &options) {
    jpeg_compress_struct cinfo;
    jpeg_error_mgr err;
    cinfo.err = jpeg_std_error(&err);
    jpeg_create_compress(&cinfo);
    unsigned char *out = nullptr;
    std::size_t size = 0;
    jpeg_mem_dest(&cinfo, &out, &size);
    cinfo.image_width = image.width;
    cinfo.image_height = image.height;
    cinfo.input_components = image.components;
    cinfo.in_color_space = image.components == 3 ? JCS_RGB : JCS_GRAYSCALE;
    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, options.quality, TRUE);
    if (options.lossless) {
        // As cjpeg -rgb1 -block 1: the transform is set before the color
        // space, whose entropy table assignment depends on it.
        cinfo.color_transform = JCT_SUBTRACT_GREEN;
        jpeg_set_colorspace(&cinfo, JCS_RGB);
        cinfo.block_size = 1;
    }
    if (options.progressive)
        jpeg_simple_progression(&cinfo);
    jpeg_start_compress(&cinfo, TRUE);
    if (!options.comment.empty())
        jpeg_write_marker(&cinfo, JPEG_COM, reinterpret_cast<const JOCTET *>(options.comment.data()),
                          static_cast<unsigned int>(options.comment.size()));
    if (!options.app2.empty())
        jpeg_write_marker(&cinfo, JPEG_APP0 + 2, options.app2.data(), static_cast<unsigned int>(options.app2.size()));
    const std::size_t stride = std::size_t(image.width) * image.components;
    while (cinfo.next_scanline < cinfo.image_height) {
        JSAMPROW row = const_cast<JSAMPROW>(&image.pixels[cinfo.next_scanline * stride]);
        jpeg_write_scanlines(&cinfo, &row, 1);
    }
    jpeg_finish_compress(&cinfo);
    jpeg_destroy_compress(&cinfo);
    Bytes stream(out, out + size);
    std::free(out);
    return stream;
}

// Reads every scanline the decompressor produces into an image.
static Image read_pixels(jpeg_decompress_struct &cinfo) {
    Image image{cinfo.output_width, cinfo.output_height, cinfo.output_components,
                Bytes(std::size_t(cinfo.output_width) * cinfo.output_height * cinfo.output_components)};
    const std::size_t stride = std::size_t(image.width) * image.components;
    while (cinfo.output_scanline < cinfo.output_height) {
        JSAMPROW row = &image.pixels[cinfo.output_scanline * stride];
        jpeg_read_scanlines(&cinfo, &row, 1);
    }
    return image;
}

static Image decode(const Bytes &stream, unsigned int scale_num = 1, unsigned int scale_denom = 1) {
    jpeg_decompress_struct cinfo;
    jpeg_error_mgr err;
    cinfo.err = jpeg_std_error(&err);
    jpeg_create_decompress(&cinfo);
    jpeg_mem_src(&cinfo, stream.data(), stream.size());
    jpeg_read_header(&cinfo, TRUE);
    cinfo.scale_num = scale_num;
    cinfo.scale_denom = scale_denom;
    jpeg_start_decompress(&cinfo);
    Image image = read_pixels(cinfo);
    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    return image;
}

static double mean_error(const Bytes &a, const Bytes &b) {
    double sum = 0;
    for (std::size_t i = 0; i < a.size(); ++i)
        sum += a[i] > b[i] ? a[i] - b[i] : b[i] - a[i];
    return sum / a.size();
}

static void round_trips_losslessly() {
    const Image image = noise(61, 37, 3);
    Options lossless;
    lossless.lossless = true;
    const Bytes stream = encode(image, lossless);

    jpeg_decompress_struct cinfo;
    jpeg_error_mgr err;
    cinfo.err = jpeg_std_error(&err);
    jpeg_create_decompress(&cinfo);
    jpeg_mem_src(&cinfo, stream.data(), stream.size());
    jpeg_read_header(&cinfo, TRUE);
    check(cinfo.jpeg_color_space == JCS_RGB && cinfo.color_transform == JCT_SUBTRACT_GREEN && cinfo.block_size == 1,
          "the lossless file is RGB with the reversible transform and 1x1 blocks");
    jpeg_start_decompress(&cinfo);
    const Image back = read_pixels(cinfo);
    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    check(back.width == 61 && back.height == 37 && back.pixels == image.pixels, "lossless mode returns every pixel");

    const Image lossy = decode(encode(image, Options()));
    check(lossy.pixels != image.pixels, "the default 8x8 YCbCr mode is lossy");
}

static void keeps_markers() {
    const Image image = ramp(40, 24);
    Options options;
    options.quality = 90;
    options.comment = "written by the C++ test";
    options.app2 = {'I', 'C', 'C', 0, 1, 2, 0xff};
    const Bytes stream = encode(image, options);

    jpeg_decompress_struct cinfo;
    jpeg_error_mgr err;
    cinfo.err = jpeg_std_error(&err);
    jpeg_create_decompress(&cinfo);
    jpeg_mem_src(&cinfo, stream.data(), stream.size());
    jpeg_save_markers(&cinfo, JPEG_COM, 0xffff);
    jpeg_save_markers(&cinfo, JPEG_APP0 + 2, 0xffff);
    jpeg_read_header(&cinfo, TRUE);
    check(cinfo.jpeg_color_space == JCS_GRAYSCALE && cinfo.num_components == 1, "the file is grayscale");
    check(cinfo.saw_JFIF_marker == TRUE, "the file starts with a JFIF marker");
    int seen = 0;
    for (jpeg_saved_marker_ptr m = cinfo.marker_list; m != nullptr; m = m->next) {
        const Bytes data(m->data, m->data + m->data_length);
        if (m->marker == JPEG_COM)
            seen += data == Bytes(options.comment.begin(), options.comment.end());
        else if (m->marker == JPEG_APP0 + 2)
            seen += data == options.app2;
    }
    check(seen == 2, "the comment and APP2 markers come back unchanged");
    jpeg_start_decompress(&cinfo);
    const Image back = read_pixels(cinfo);
    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    check(back.components == 1 && mean_error(back.pixels, image.pixels) < 1.5, "quality 90 keeps the ramp");
}

// The mean distance of a scaled decode of ramp(40, 24) from the ramp at
// each output pixel's center.
static double ramp_error(const Image &scaled, double scale) {
    double error = 0;
    for (JDIMENSION y = 0; y < scaled.height; ++y)
        for (JDIMENSION x = 0; x < scaled.width; ++x) {
            const double expected = 4 * ((x + 0.5) / scale - 0.5) + 2 * ((y + 0.5) / scale - 0.5);
            const double got = scaled.pixels[std::size_t(y) * scaled.width + x];
            error += got > expected ? got - expected : expected - got;
        }
    return error / (scaled.width * scaled.height);
}

static void scales_while_decoding() {
    const Bytes stream = encode(ramp(40, 24), Options());
    const Image quarter = decode(stream, 1, 4);
    check(quarter.width == 10 && quarter.height == 6, "1/4 scaling gives 10x6");
    check(ramp_error(quarter, 0.25) < 2, "the 1/4 image is the ramp at a quarter size");
    // libjpeg 9 scales by any n/8.
    const Image three_eighths = decode(stream, 3, 8);
    check(three_eighths.width == 15 && three_eighths.height == 9, "3/8 scaling gives 15x9");
    check(ramp_error(three_eighths, 0.375) < 2, "the 3/8 image is the ramp at 3/8 size");
}

struct Quantized {
    int colors;
    bool in_range;
    Bytes mapped;  // each pixel's colormap entry
};

static Quantized quantize(const Bytes &stream, int desired) {
    jpeg_decompress_struct cinfo;
    jpeg_error_mgr err;
    cinfo.err = jpeg_std_error(&err);
    jpeg_create_decompress(&cinfo);
    jpeg_mem_src(&cinfo, stream.data(), stream.size());
    jpeg_read_header(&cinfo, TRUE);
    cinfo.quantize_colors = TRUE;
    cinfo.desired_number_of_colors = desired;
    cinfo.two_pass_quantize = TRUE;
    jpeg_start_decompress(&cinfo);
    check(cinfo.output_components == 1, "quantized output is one index per pixel");
    Quantized result{cinfo.actual_number_of_colors, true, Bytes()};
    const Image indices = read_pixels(cinfo);
    result.mapped.resize(indices.pixels.size() * 3);
    for (std::size_t i = 0; i < indices.pixels.size(); ++i) {
        const int index = indices.pixels[i];
        result.in_range = result.in_range && index < result.colors;
        for (std::size_t c = 0; c < 3; ++c)
            result.mapped[i * 3 + c] = index < result.colors ? cinfo.colormap[c][index] : 0;
    }
    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    return result;
}

static void quantizes_colors() {
    // Four flat quadrants, which a working quantizer maps back close to
    // their decoded colors.
    static const unsigned char quadrant[4][3] = {{200, 30, 30}, {30, 200, 30}, {30, 30, 200}, {128, 128, 128}};
    Image image{32, 32, 3, Bytes(32 * 32 * 3)};
    for (std::size_t y = 0; y < 32; ++y)
        for (std::size_t x = 0; x < 32; ++x)
            for (std::size_t c = 0; c < 3; ++c)
                image.pixels[(y * 32 + x) * 3 + c] = quadrant[(y / 16) * 2 + x / 16][c];
    Options options;
    options.quality = 95;
    const Bytes stream = encode(image, options);
    const Quantized quadrants = quantize(stream, 16);
    check(quadrants.colors >= 2 && quadrants.colors <= 16, "two-pass quantization chooses at most 16 colors");
    check(quadrants.in_range, "every pixel indexes the colormap");
    check(mean_error(quadrants.mapped, decode(stream).pixels) < 5, "the colormapped pixels match a plain decode");
    // Noise has far more colors than asked for, so all 16 are used.
    check(quantize(encode(noise(32, 32, 3), Options()), 16).colors == 16, "noise quantizes to exactly 16 colors");
}

static void decodes_progressively() {
    const Image image = ramp(40, 24);
    Options options;
    options.progressive = true;
    const Bytes stream = encode(image, options);
    const Image whole = decode(stream);

    jpeg_decompress_struct cinfo;
    jpeg_error_mgr err;
    cinfo.err = jpeg_std_error(&err);
    jpeg_create_decompress(&cinfo);
    jpeg_mem_src(&cinfo, stream.data(), stream.size());
    jpeg_read_header(&cinfo, TRUE);
    check(cinfo.progressive_mode == TRUE, "the file is progressive");
    cinfo.buffered_image = TRUE;
    jpeg_start_decompress(&cinfo);
    // One output pass per scan, then a final one once the EOI is read.
    int passes = 0;
    Image first, last;
    for (;;) {
        jpeg_start_output(&cinfo, cinfo.input_scan_number);
        last = read_pixels(cinfo);
        jpeg_finish_output(&cinfo);
        if (++passes == 1)
            first = last;
        if (jpeg_input_complete(&cinfo))
            break;
        int status;
        do
            status = jpeg_consume_input(&cinfo);
        while (status != JPEG_REACHED_SOS && status != JPEG_REACHED_EOI);
    }
    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    check(passes > 2 && first.pixels != whole.pixels, "the first scans give a coarser image");
    check(last.pixels == whole.pixels, "the final buffered pass is the whole image");
}

int main() {
    round_trips_losslessly();
    keeps_markers();
    scales_while_decoding();
    quantizes_colors();
    decodes_progressively();
    std::printf("libjpeg (C++): %s\n", ok ? "ok" : "FAILED");
    return ok ? 0 : 1;
}
