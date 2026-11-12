#include <cgif.h>
#include <cgif_raw.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

static void check(int ok, const char* what) {
    if (!ok) {
        fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

/* Growable output for the stream API; `limit` (when nonzero) makes writes
 * past that many bytes fail. */
typedef struct {
    uint8_t* data;
    size_t size;
    size_t cap;
    size_t limit;
} Buffer;

static int append(void* context, const uint8_t* bytes, const size_t count) {
    Buffer* out = (Buffer*)context;
    if (out->limit != 0 && out->size + count > out->limit) {
        return -1;
    }
    if (out->size + count > out->cap) {
        size_t cap = out->cap != 0 ? out->cap : 1024;
        uint8_t* grown;
        while (cap < out->size + count) {
            cap *= 2;
        }
        grown = (uint8_t*)realloc(out->data, cap);
        if (grown == NULL) {
            return -1;
        }
        out->data = grown;
        out->cap = cap;
    }
    if (count != 0) {
        memcpy(out->data + out->size, bytes, count);
    }
    out->size += count;
    return 0;
}

static unsigned u16le(const uint8_t* p) {
    return (unsigned)p[0] | ((unsigned)p[1] << 8);
}

/* An independent GIF LZW decoder: reads the code size and the data
 * sub-blocks at *pos and requires them to decode to exactly `expected`. */
static int raster_matches(const Buffer* gif, size_t* pos, const uint8_t* expected, size_t count) {
    static uint16_t prefix[4096];
    static uint8_t suffix[4096];
    static uint8_t stack[4097];
    uint8_t* codes;
    uint8_t* pixels;
    size_t length = 0, bit = 0, produced = 0;
    unsigned min_size, clear, next, width;
    int prev = -1, ok = 0;
    uint8_t first = 0;

    if (*pos >= gif->size) {
        return 0;
    }
    min_size = gif->data[(*pos)++];
    if (min_size < 2 || min_size > 8) {
        return 0;
    }
    codes = (uint8_t*)malloc(gif->size);
    pixels = (uint8_t*)malloc(count);
    if (codes == NULL || pixels == NULL) {
        goto done;
    }
    for (;;) {
        size_t block;
        if (*pos >= gif->size) {
            goto done;
        }
        block = gif->data[(*pos)++];
        if (block == 0) {
            break;
        }
        if (*pos + block > gif->size) {
            goto done;
        }
        memcpy(codes + length, gif->data + *pos, block);
        length += block;
        *pos += block;
    }

    clear = 1u << min_size;
    next = clear + 2;
    width = min_size + 1;
    for (;;) {
        unsigned code = 0, cur, i;
        size_t depth = 0;
        if (bit + width > length * 8) {
            goto done;
        }
        for (i = 0; i < width; ++i, ++bit) {
            code |= (unsigned)((codes[bit / 8] >> (bit % 8)) & 1u) << i;
        }
        if (code == clear) {
            next = clear + 2;
            width = min_size + 1;
            prev = -1;
            continue;
        }
        if (code == clear + 1) {
            break;
        }
        if (prev < 0) {
            if (code >= clear || produced == count) {
                goto done;
            }
            pixels[produced++] = (uint8_t)code;
            first = (uint8_t)code;
            prev = (int)code;
            continue;
        }
        if (code < next) {
            cur = code;
        } else if (code == next) {
            stack[depth++] = first;
            cur = (unsigned)prev;
        } else {
            goto done;
        }
        while (cur > clear) {
            stack[depth++] = suffix[cur];
            cur = prefix[cur];
        }
        if (cur == clear) {
            goto done;
        }
        stack[depth++] = (uint8_t)cur;
        first = (uint8_t)cur;
        if (produced + depth > count) {
            goto done;
        }
        while (depth > 0) {
            pixels[produced++] = stack[--depth];
        }
        if (next < 4096) {
            prefix[next] = (uint16_t)prev;
            suffix[next] = first;
            ++next;
            if (next == (1u << width) && width < 12) {
                ++width;
            }
        }
        prev = (int)code;
    }
    /* Nothing may follow the end code. */
    ok = (bit + 7) / 8 == length && produced == count && memcmp(pixels, expected, count) == 0;
done:
    free(codes);
    free(pixels);
    return ok;
}

/* Checks the 10-byte image descriptor at *pos. */
static int descriptor_matches(const Buffer* gif, size_t* pos, unsigned left, unsigned top, unsigned width,
                              unsigned height, unsigned packed) {
    const uint8_t* p;
    if (*pos + 10 > gif->size) {
        return 0;
    }
    p = gif->data + *pos;
    *pos += 10;
    return p[0] == ',' && u16le(p + 1) == left && u16le(p + 3) == top && u16le(p + 5) == width &&
           u16le(p + 7) == height && p[9] == packed;
}

enum { WIDTH = 300, HEIGHT = 200 };

static uint8_t image[WIDTH * HEIGHT];
static uint8_t palette[5 * 3] = {0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255};

/* Noise over all five palette entries, with striped rows for long runs:
 * enough codes to reach 12-bit codes and refill the dictionary. */
static void fill_image(void) {
    uint32_t seed = 12345;
    int x, y;
    for (y = 0; y < HEIGHT; ++y) {
        for (x = 0; x < WIDTH; ++x) {
            seed = seed * 1103515245u + 12345u;
            image[y * WIDTH + x] = (uint8_t)(y % 4 == 0 ? (x / 7) % 5 : (seed >> 16) % 5);
        }
    }
}

static int encode_still(Buffer* out, const char* path) {
    CGIF_Config config;
    CGIF_FrameConfig frame;
    CGIF* gif;
    int added;

    memset(&config, 0, sizeof config);
    config.pGlobalPalette = palette;
    config.numGlobalPaletteEntries = 5;
    config.width = WIDTH;
    config.height = HEIGHT;
    config.path = path;
    if (path == NULL) {
        config.pWriteFn = append;
        config.pContext = out;
    }
    gif = cgif_newgif(&config);
    if (gif == NULL) {
        return CGIF_ERROR;
    }
    memset(&frame, 0, sizeof frame);
    frame.pImageData = image;
    added = cgif_addframe(gif, &frame);
    return cgif_close(gif) == CGIF_OK ? added : CGIF_ERROR;
}

static void still_image(Buffer* out) {
    static const uint8_t header[] = {'G', 'I', 'F', '8', '9', 'a', WIDTH % 256, WIDTH / 256, HEIGHT % 256, HEIGHT / 256,
                                     0x82, 0, 0};
    static const uint8_t padding[9] = {0};
    size_t pos = sizeof header + sizeof palette + sizeof padding;

    check(encode_still(out, NULL) == CGIF_OK, "still: encode");
    check(out->size > pos && memcmp(out->data, header, sizeof header) == 0, "still: header");
    if (failures != 0) {
        return;
    }
    check(memcmp(out->data + sizeof header, palette, sizeof palette) == 0 &&
              memcmp(out->data + sizeof header + sizeof palette, padding, sizeof padding) == 0,
          "still: global colour table padded to 8 entries");
    check(descriptor_matches(out, &pos, 0, 0, WIDTH, HEIGHT, 0), "still: image descriptor");
    check(pos < out->size && out->data[pos] == 3, "still: LZW minimum code size");
    check(raster_matches(out, &pos, image, sizeof image), "still: raster decodes to the input");
    check(pos + 1 == out->size && out->data[pos] == ';', "still: trailer");
}

/* The path API writes the same bytes the stream API does. */
static void path_output(const Buffer* stream) {
    char path[4096];
    const char* dir = getenv("CABIN_PROFILE_DIR");
    Buffer file = {0};
    FILE* in;

    if (dir == NULL || strlen(dir) + 32 > sizeof path) {
        check(0, "path: CABIN_PROFILE_DIR");
        return;
    }
    snprintf(path, sizeof path, "%s/port-probe-cgif-c.gif", dir);
    check(encode_still(NULL, path) == CGIF_OK, "path: encode");
    in = fopen(path, "rb");
    check(in != NULL, "path: reopen");
    if (in != NULL) {
        uint8_t chunk[4096];
        size_t got;
        while ((got = fread(chunk, 1, sizeof chunk, in)) > 0) {
            append(&file, chunk, got);
        }
        fclose(in);
    }
    remove(path);
    check(file.size == stream->size && memcmp(file.data, stream->data, file.size) == 0, "path: file equals stream");
    free(file.data);
}

static size_t expect_control(const Buffer* gif, size_t pos, unsigned delay, int* ok) {
    const uint8_t* p;
    *ok = 0;
    if (pos + 8 <= gif->size) {
        p = gif->data + pos;
        *ok = p[0] == 0x21 && p[1] == 0xF9 && p[2] == 4 && (p[3] & 1) == 0 && u16le(p + 4) == delay && p[7] == 0;
    }
    return pos + 8;
}

/* An animation keeps distinct frames and folds an identical successor into
 * its predecessor, summing the delays. */
static void animation(void) {
    enum { W = 16, H = 8 };
    static const uint8_t netscape[19] = {0x21, 0xFF, 0x0B, 'N', 'E', 'T', 'S', 'C', 'A', 'P',
                                         'E',  '2',  '.',  '0', 3,   1,   3,   0,   0};
    uint8_t two_colours[6] = {0, 0, 0, 255, 255, 255};
    uint8_t first[W * H], second[W * H];
    CGIF_Config config;
    CGIF_FrameConfig frame;
    Buffer out = {0};
    CGIF* gif;
    size_t pos = 13 + 6;
    int i, ok;

    for (i = 0; i < W * H; ++i) {
        first[i] = (uint8_t)((i / W + i % W) % 2);
        second[i] = (uint8_t)(i % 3 == 0);
    }
    memset(&config, 0, sizeof config);
    config.pGlobalPalette = two_colours;
    config.numGlobalPaletteEntries = 2;
    config.attrFlags = CGIF_ATTR_IS_ANIMATED;
    config.numLoops = 3;
    config.width = W;
    config.height = H;
    config.pWriteFn = append;
    config.pContext = &out;
    gif = cgif_newgif(&config);
    check(gif != NULL, "animation: newgif");
    if (gif == NULL) {
        return;
    }
    memset(&frame, 0, sizeof frame);
    frame.pImageData = first;
    frame.delay = 10;
    check(cgif_addframe(gif, &frame) == CGIF_OK, "animation: first frame");
    frame.pImageData = second;
    frame.delay = 20;
    check(cgif_addframe(gif, &frame) == CGIF_OK, "animation: second frame");
    frame.delay = 30;
    check(cgif_addframe(gif, &frame) == CGIF_OK, "animation: repeated frame");
    check(cgif_close(gif) == CGIF_OK, "animation: close");

    check(out.size > pos + sizeof netscape && out.data[10] == 0x80 &&
              memcmp(out.data + pos, netscape, sizeof netscape) == 0,
          "animation: NETSCAPE2.0 loop count");
    if (failures == 0) {
        pos += sizeof netscape;
        pos = expect_control(&out, pos, 10, &ok);
        check(ok, "animation: first frame delay");
        check(descriptor_matches(&out, &pos, 0, 0, W, H, 0) && pos < out.size && out.data[pos] == 2 &&
                  raster_matches(&out, &pos, first, W * H),
              "animation: first frame raster");
        pos = expect_control(&out, pos, 50, &ok);
        check(ok, "animation: repeated frame folded into the second");
        check(descriptor_matches(&out, &pos, 0, 0, W, H, 0) && raster_matches(&out, &pos, second, W * H),
              "animation: second frame raster");
        check(pos + 1 == out.size && out.data[pos] == ';', "animation: exactly two frames");
    }
    free(out.data);
}

/* The low-level API writes exactly the frame it is given: here a 3x2
 * sub-frame at (1, 2) with a transparent index and a disposal method. */
static void raw_api(void) {
    static const uint8_t control[8] = {0x21, 0xF9, 4, DISPOSAL_METHOD_PREVIOUS | 1, 25, 0, 1, 0};
    uint8_t two_colours[6] = {0, 0, 0, 255, 255, 255};
    uint8_t pixels[6] = {1, 0, 1, 0, 1, 1};
    CGIFRaw_Config config;
    CGIFRaw_FrameConfig frame;
    Buffer out = {0};
    CGIFRaw* gif;
    size_t pos = 13 + 6 + sizeof control;

    memset(&config, 0, sizeof config);
    config.pWriteFn = append;
    config.pContext = &out;
    config.pGCT = two_colours;
    config.sizeGCT = 2;
    config.width = 6;
    config.height = 5;
    gif = cgif_raw_newgif(&config);
    check(gif != NULL, "raw: newgif");
    if (gif == NULL) {
        return;
    }
    memset(&frame, 0, sizeof frame);
    frame.pImageData = pixels;
    frame.attrFlags = CGIF_RAW_FRAME_ATTR_HAS_TRANS;
    frame.width = 3;
    frame.height = 2;
    frame.left = 1;
    frame.top = 2;
    frame.delay = 25;
    frame.disposalMethod = DISPOSAL_METHOD_PREVIOUS;
    frame.transIndex = 1;
    check(cgif_raw_addframe(gif, &frame) == CGIF_OK, "raw: addframe");
    check(cgif_raw_close(gif) == CGIF_OK, "raw: close");
    check(out.size > pos && out.data[10] == 0x80 && memcmp(out.data + 19, control, sizeof control) == 0,
          "raw: graphic control extension");
    check(descriptor_matches(&out, &pos, 1, 2, 3, 2, 0) && raster_matches(&out, &pos, pixels, sizeof pixels),
          "raw: sub-frame");
    check(pos + 1 == out.size && out.data[pos] == ';', "raw: trailer");
    free(out.data);
}

static void errors(void) {
    uint8_t bad[WIDTH * HEIGHT];
    CGIF_Config config;
    CGIF_FrameConfig frame;
    Buffer out = {0};
    CGIF* gif;

    memset(&config, 0, sizeof config);
    config.pGlobalPalette = palette;
    config.numGlobalPaletteEntries = 5;
    config.width = WIDTH;
    config.pWriteFn = append;
    config.pContext = &out;
    check(cgif_newgif(&config) == NULL, "errors: zero height rejected");

    config.height = HEIGHT;
    gif = cgif_newgif(&config);
    check(gif != NULL && cgif_close(gif) == CGIF_ERROR, "errors: a GIF without frames");

    /* The frame is encoded at the latest by cgif_close, which reports it. */
    memcpy(bad, image, sizeof bad);
    bad[1234] = 200;
    memset(&frame, 0, sizeof frame);
    frame.pImageData = bad;
    out.size = 0;
    gif = cgif_newgif(&config);
    check(gif != NULL, "errors: newgif");
    if (gif != NULL) {
        cgif_addframe(gif, &frame);
        check(cgif_close(gif) == CGIF_EINDEX, "errors: index beyond the colour table");
    }

    /* A failing writer is reported, not ignored. */
    out.size = 0;
    out.limit = 64;
    frame.pImageData = image;
    gif = cgif_newgif(&config);
    check(gif != NULL, "errors: newgif under a write limit");
    if (gif != NULL) {
        cgif_addframe(gif, &frame);
        check(cgif_close(gif) == CGIF_EWRITE, "errors: write failure");
    }
    free(out.data);
}

int main(void) {
    Buffer still = {0};

    fill_image();
    still_image(&still);
    if (failures == 0) {
        path_output(&still);
    }
    animation();
    raw_api();
    errors();
    free(still.data);
    if (failures != 0) {
        return 1;
    }
    puts("cgif C consumer: OK");
    return 0;
}
