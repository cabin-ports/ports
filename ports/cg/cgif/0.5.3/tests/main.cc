#include <cgif.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

// cgif_write_fn has C language linkage.
extern "C" {

static int append(void* context, const std::uint8_t* data, const std::size_t size) {
    std::vector<std::uint8_t>& out = *static_cast<std::vector<std::uint8_t>*>(context);
    out.insert(out.end(), data, data + size);
    return 0;
}
}

namespace {

using Bytes = std::vector<std::uint8_t>;

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

unsigned u16le(const Bytes& b, std::size_t at) {
    return b[at] | (b[at + 1] << 8);
}

bool bytes_at(const Bytes& b, std::size_t at, const Bytes& want) {
    if (at + want.size() > b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < want.size(); ++i) {
        if (b[at + i] != want[i]) {
            return false;
        }
    }
    return true;
}

// An independent GIF LZW decoder over the code size and data sub-blocks at
// `pos`; fails on malformed data.
bool decode(const Bytes& gif, std::size_t& pos, Bytes& pixels) {
    if (pos >= gif.size()) {
        return false;
    }
    const unsigned min_size = gif[pos++];
    if (min_size < 2 || min_size > 8) {
        return false;
    }
    Bytes codes;
    for (;;) {
        if (pos >= gif.size()) {
            return false;
        }
        const std::size_t block = gif[pos++];
        if (block == 0) {
            break;
        }
        if (pos + block > gif.size()) {
            return false;
        }
        codes.insert(codes.end(), gif.begin() + pos, gif.begin() + pos + block);
        pos += block;
    }

    const unsigned clear = 1u << min_size;
    std::vector<std::uint16_t> prefix(4096);
    Bytes suffix(4096), stack;
    unsigned next = clear + 2, width = min_size + 1;
    int prev = -1;
    std::uint8_t first = 0;
    std::size_t bit = 0;
    pixels.clear();
    for (;;) {
        if (bit + width > codes.size() * 8) {
            return false;
        }
        unsigned code = 0;
        for (unsigned i = 0; i < width; ++i, ++bit) {
            code |= ((codes[bit / 8] >> (bit % 8)) & 1u) << i;
        }
        if (code == clear) {
            next = clear + 2;
            width = min_size + 1;
            prev = -1;
            continue;
        }
        if (code == clear + 1) {
            return (bit + 7) / 8 == codes.size();  // nothing after the end code
        }
        if (prev < 0) {
            if (code >= clear) {
                return false;
            }
            first = static_cast<std::uint8_t>(code);
            pixels.push_back(first);
            prev = static_cast<int>(code);
            continue;
        }
        unsigned cur = code;
        stack.clear();
        if (code == next) {
            stack.push_back(first);
            cur = static_cast<unsigned>(prev);
        } else if (code > next) {
            return false;
        }
        while (cur > clear) {
            stack.push_back(suffix[cur]);
            cur = prefix[cur];
        }
        if (cur == clear) {
            return false;
        }
        first = static_cast<std::uint8_t>(cur);
        stack.push_back(first);
        pixels.insert(pixels.end(), stack.rbegin(), stack.rend());
        if (next < 4096) {
            prefix[next] = static_cast<std::uint16_t>(prev);
            suffix[next] = first;
            if (++next == (1u << width) && width < 12) {
                ++width;
            }
        }
        prev = static_cast<int>(code);
    }
}

bool frame_matches(const Bytes& gif, std::size_t& pos, unsigned left, unsigned top, unsigned width, unsigned height,
                   unsigned packed, const Bytes& pixels) {
    if (pos + 10 > gif.size() || gif[pos] != ',' || u16le(gif, pos + 1) != left || u16le(gif, pos + 3) != top ||
        u16le(gif, pos + 5) != width || u16le(gif, pos + 7) != height || gif[pos + 9] != packed) {
        return false;
    }
    pos += 10;
    if (packed & 0x80) {
        pos += 3u << ((packed & 7) + 1);
    }
    Bytes decoded;
    return decode(gif, pos, decoded) && decoded == pixels;
}

// A still image with no global table: a local table of three colours, the
// third transparent, rows stored in the four interlace passes.
void interlaced_local_table() {
    const unsigned width = 10, height = 13;
    Bytes image(width * height);
    for (std::size_t i = 0; i < image.size(); ++i) {
        image[i] = static_cast<std::uint8_t>((i * 7 + i / width) % 3);
    }
    std::uint8_t colours[] = {255, 0, 0, 0, 0, 255, 0, 0, 0};

    Bytes out;
    CGIF_Config config{};
    config.attrFlags = CGIF_ATTR_NO_GLOBAL_TABLE;
    config.width = width;
    config.height = height;
    config.pWriteFn = append;
    config.pContext = &out;
    CGIF* gif = cgif_newgif(&config);
    check(gif != nullptr, "interlaced: newgif");
    if (gif == nullptr) {
        return;
    }
    CGIF_FrameConfig frame{};
    frame.attrFlags = CGIF_FRAME_ATTR_USE_LOCAL_TABLE | CGIF_FRAME_ATTR_INTERLACED | CGIF_FRAME_ATTR_HAS_ALPHA;
    frame.pLocalPalette = colours;
    frame.numLocalPaletteEntries = 3;
    frame.transIndex = 2;
    frame.pImageData = image.data();
    check(cgif_addframe(gif, &frame) == CGIF_OK, "interlaced: addframe");
    check(cgif_close(gif) == CGIF_OK, "interlaced: close");

    Bytes rows;
    const unsigned starts[] = {0, 4, 2, 1}, steps[] = {8, 8, 4, 2};
    for (int pass = 0; pass < 4; ++pass) {
        for (unsigned row = starts[pass]; row < height; row += steps[pass]) {
            rows.insert(rows.end(), image.begin() + row * width, image.begin() + (row + 1) * width);
        }
    }

    check(bytes_at(out, 0, {'G', 'I', 'F', '8', '9', 'a', width, 0, height, 0, 0, 0, 0}), "interlaced: header");
    // Graphic control extension: transparency flag and index.
    check(out.size() > 21 && bytes_at(out, 13, {0x21, 0xF9, 4}) && (out[16] & 1) == 1 && out[19] == 2 && out[20] == 0,
          "interlaced: transparent index");
    check(bytes_at(out, 31, {255, 0, 0, 0, 0, 255, 0, 0, 0, 0, 0, 0}), "interlaced: local table padded to 4 entries");
    std::size_t pos = 21;
    check(frame_matches(out, pos, 0, 0, width, height, 0x80 | 0x40 | 1, rows), "interlaced: raster in pass order");
    check(pos + 1 == out.size() && out[pos] == ';', "interlaced: trailer");
}

// With CGIF_FRAME_GEN_USE_DIFF_WINDOW, a frame encodes only the rectangle
// that differs from its predecessor.
void diff_window() {
    const unsigned width = 12, height = 10;
    Bytes before(width * height);
    for (std::size_t i = 0; i < before.size(); ++i) {
        before[i] = static_cast<std::uint8_t>((i * 5 + i / 3) % 4);
    }
    Bytes after = before;
    // The changes span rows 3-6 and columns 2-8.
    after[3 * width + 5] ^= 1;
    after[6 * width + 4] ^= 2;
    after[4 * width + 2] ^= 3;
    after[5 * width + 8] ^= 1;
    std::uint8_t colours[] = {0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255};

    Bytes out;
    CGIF_Config config{};
    config.attrFlags = CGIF_ATTR_IS_ANIMATED | CGIF_ATTR_NO_LOOP;
    config.pGlobalPalette = colours;
    config.numGlobalPaletteEntries = 4;
    config.width = width;
    config.height = height;
    config.pWriteFn = append;
    config.pContext = &out;
    CGIF* gif = cgif_newgif(&config);
    check(gif != nullptr, "diff window: newgif");
    if (gif == nullptr) {
        return;
    }
    CGIF_FrameConfig frame{};
    frame.genFlags = CGIF_FRAME_GEN_USE_DIFF_WINDOW;
    frame.delay = 7;
    frame.pImageData = before.data();
    check(cgif_addframe(gif, &frame) == CGIF_OK, "diff window: first frame");
    frame.pImageData = after.data();
    check(cgif_addframe(gif, &frame) == CGIF_OK, "diff window: second frame");
    check(cgif_close(gif) == CGIF_OK, "diff window: close");

    Bytes window;
    for (unsigned row = 3; row <= 6; ++row) {
        window.insert(window.end(), after.begin() + row * width + 2, after.begin() + row * width + 9);
    }

    // CGIF_ATTR_NO_LOOP: no NETSCAPE2.0 block after the 4-entry table.
    std::size_t pos = 13 + 12;
    check(out.size() > pos + 1 && out[10] == 0x81 && out[pos] == 0x21 && out[pos + 1] == 0xF9, "diff window: no loop block");
    pos += 8;
    check(frame_matches(out, pos, 0, 0, width, height, 0, before), "diff window: first frame is the full canvas");
    check(pos + 8 <= out.size() && bytes_at(out, pos, {0x21, 0xF9, 4}) && u16le(out, pos + 4) == 7, "diff window: second frame delay");
    pos += 8;
    check(frame_matches(out, pos, 2, 3, 7, 4, 0, window), "diff window: second frame is the changed rectangle");
    check(pos + 1 == out.size() && out[pos] == ';', "diff window: trailer");
}

}  // namespace

int main() {
    interlaced_local_table();
    diff_window();
    if (failures != 0) {
        return 1;
    }
    std::puts("cgif C++ consumer: OK");
    return 0;
}
