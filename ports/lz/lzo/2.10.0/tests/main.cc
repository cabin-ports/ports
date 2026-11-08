// Round-trips one buffer through every algorithm family the library
// builds (LZO1 to LZO2A, fast and best-ratio compressors), checks each
// decoder's safe variant against an output buffer one byte short, and
// optimizes an LZO1X block in place.
// The unprefixed form that lzo2.pc's include path supports.
#include <lzo1.h>
#include <lzo1a.h>
#include <lzo1b.h>
#include <lzo1c.h>
#include <lzo1f.h>
#include <lzo1x.h>
#include <lzo1y.h>
#include <lzo1z.h>
#include <lzo2a.h>

#include <cstddef>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct Codec {
    const char* name;
    lzo_compress_t compress;
    lzo_uint32_t wrkmem_size;
    lzo_decompress_t decompress;
    lzo_decompress_t decompress_safe;  // 0 where the family has none
};

const Codec codecs[] = {
    {"lzo1", lzo1_compress, LZO1_MEM_COMPRESS, lzo1_decompress, 0},
    {"lzo1_99", lzo1_99_compress, LZO1_99_MEM_COMPRESS, lzo1_decompress, 0},
    {"lzo1a", lzo1a_compress, LZO1A_MEM_COMPRESS, lzo1a_decompress, 0},
    {"lzo1a_99", lzo1a_99_compress, LZO1A_99_MEM_COMPRESS, lzo1a_decompress, 0},
    {"lzo1b_1", lzo1b_1_compress, LZO1B_MEM_COMPRESS, lzo1b_decompress, lzo1b_decompress_safe},
    {"lzo1b_9", lzo1b_9_compress, LZO1B_MEM_COMPRESS, lzo1b_decompress, lzo1b_decompress_safe},
    {"lzo1b_999", lzo1b_999_compress, LZO1B_999_MEM_COMPRESS, lzo1b_decompress,
     lzo1b_decompress_safe},
    {"lzo1c_1", lzo1c_1_compress, LZO1C_MEM_COMPRESS, lzo1c_decompress, lzo1c_decompress_safe},
    {"lzo1c_999", lzo1c_999_compress, LZO1C_999_MEM_COMPRESS, lzo1c_decompress,
     lzo1c_decompress_safe},
    {"lzo1f_1", lzo1f_1_compress, LZO1F_MEM_COMPRESS, lzo1f_decompress, lzo1f_decompress_safe},
    {"lzo1f_999", lzo1f_999_compress, LZO1F_999_MEM_COMPRESS, lzo1f_decompress,
     lzo1f_decompress_safe},
    {"lzo1x_1_11", lzo1x_1_11_compress, LZO1X_1_11_MEM_COMPRESS, lzo1x_decompress,
     lzo1x_decompress_safe},
    {"lzo1x_1_15", lzo1x_1_15_compress, LZO1X_1_15_MEM_COMPRESS, lzo1x_decompress,
     lzo1x_decompress_safe},
    {"lzo1y_1", lzo1y_1_compress, LZO1Y_MEM_COMPRESS, lzo1y_decompress, lzo1y_decompress_safe},
    {"lzo1y_999", lzo1y_999_compress, LZO1Y_999_MEM_COMPRESS, lzo1y_decompress,
     lzo1y_decompress_safe},
    {"lzo1z_999", lzo1z_999_compress, LZO1Z_999_MEM_COMPRESS, lzo1z_decompress,
     lzo1z_decompress_safe},
    {"lzo2a_999", lzo2a_999_compress, LZO2A_999_MEM_COMPRESS, lzo2a_decompress,
     lzo2a_decompress_safe},
};

bool ok = true;

void check(bool pass, const char* codec, const char* what) {
    if (!pass) {
        std::fprintf(stderr, "FAIL: %s: %s\n", codec, what);
        ok = false;
    }
}

}  // namespace

int main() {
    check(lzo_init() == LZO_E_OK, "lzo", "lzo_init");

    // Sentences with repeats near and far, then a stretch of one byte.
    std::ostringstream text;
    for (unsigned i = 0; i < 1500; ++i)
        text << "line " << i % 97 << " of " << i % 13 << ": the quick brown fox; ";
    const std::string sentences = text.str();
    std::vector<unsigned char> data(sentences.begin(), sentences.end());
    data.insert(data.end(), 5000, 'z');
    const lzo_uint len = data.size();

    for (std::size_t c = 0; c < sizeof codecs / sizeof codecs[0]; ++c) {
        const Codec& codec = codecs[c];
        std::vector<unsigned char> wrkmem(codec.wrkmem_size);
        std::vector<unsigned char> packed(len + len / 16 + 64 + 3);
        lzo_uint packed_len = packed.size();
        int r = codec.compress(&data[0], len, &packed[0], &packed_len, &wrkmem[0]);
        check(r == LZO_E_OK && packed_len < len / 2, codec.name, "compresses");

        std::vector<unsigned char> out(len);
        lzo_uint out_len = len;
        r = codec.decompress(&packed[0], packed_len, &out[0], &out_len, 0);
        check(r == LZO_E_OK && out_len == len && out == data, codec.name, "decompress");
        if (codec.decompress_safe) {
            out.assign(len, 0);
            out_len = len;
            r = codec.decompress_safe(&packed[0], packed_len, &out[0], &out_len, 0);
            check(r == LZO_E_OK && out_len == len && out == data, codec.name, "safe decompress");
            out_len = len - 1;
            r = codec.decompress_safe(&packed[0], packed_len, &out[0], &out_len, 0);
            check(r == LZO_E_OUTPUT_OVERRUN, codec.name, "output one byte short");
        }
    }

    // lzo1x_optimize rewrites a block in place for faster decoding; it
    // must still decode to the same bytes.
    std::vector<unsigned char> wrkmem(LZO1X_999_MEM_COMPRESS);
    std::vector<unsigned char> packed(len + len / 16 + 64 + 3);
    lzo_uint packed_len = packed.size();
    int r = lzo1x_999_compress(&data[0], len, &packed[0], &packed_len, &wrkmem[0]);
    std::vector<unsigned char> out(len);
    lzo_uint out_len = len;
    check(r == LZO_E_OK &&
              lzo1x_optimize(&packed[0], packed_len, &out[0], &out_len, 0) == LZO_E_OK &&
              out_len == len && out == data,
          "lzo1x", "optimize");
    out.assign(len, 0);
    out_len = len;
    r = lzo1x_decompress_safe(&packed[0], packed_len, &out[0], &out_len, 0);
    check(r == LZO_E_OK && out == data, "lzo1x", "decompress after optimize");

    std::printf("lzo (C++): %s\n", ok ? "ok" : "FAILED");
    return ok ? 0 : 1;
}
