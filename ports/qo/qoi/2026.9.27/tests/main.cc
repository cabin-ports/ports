// A C++98 consumer of qoi: builds the implementation as C++ with
// QOI_NO_STDIO and its own QOI_MALLOC, checks the encoded bytes of a small
// RGBA image against the QOI specification, decodes it back, and checks that
// each call allocated its result through QOI_MALLOC.
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

int allocations = 0;

void* counted_malloc(std::size_t size) {
  ++allocations;
  return std::malloc(size);
}

}  // namespace

#define QOI_NO_STDIO
#define QOI_MALLOC(sz) counted_malloc(sz)
#define QOI_IMPLEMENTATION
#include "qoi.h"

namespace {

int failures = 0;

#define CHECK(cond)                                               \
  do {                                                            \
    if (!(cond)) {                                                \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      ++failures;                                                 \
    }                                                             \
  } while (0)

// 2x2 RGBA: a DIFF, a LUMA, an RGBA, then an INDEX hit on pixel 0.
const unsigned char kPixels[4 * 4] = {
    1, 1, 0, 255, 18, 21, 25, 255, 200, 10, 100, 128, 1, 1, 0, 255,
};

const unsigned char kEncoded[] = {
    'q',  'o', 'i', 'f', 0, 0, 0, 2, 0, 0, 0, 2, 4, 0,
    0x7e,                       // diff +1 +1 0
    0xb4, 0x5d,                 // luma: g +20, r-g -3, b-g +5
    0xff, 200, 10,  100, 128,   // rgba
    0x3d,                       // index 61
    0,    0,   0,   0,   0, 0, 0, 1,
};

}  // namespace

int main() {
  qoi_desc desc;
  desc.width = 2;
  desc.height = 2;
  desc.channels = 4;
  desc.colorspace = QOI_SRGB;
  int len = 0;
  unsigned char* encoded =
      static_cast<unsigned char*>(qoi_encode(kPixels, &desc, &len));
  CHECK(encoded != NULL && len == static_cast<int>(sizeof kEncoded) &&
        std::memcmp(encoded, kEncoded, sizeof kEncoded) == 0);
  CHECK(allocations == 1);

  qoi_desc out;
  std::memset(&out, 0, sizeof out);
  unsigned char* decoded =
      static_cast<unsigned char*>(qoi_decode(encoded, len, &out, 0));
  CHECK(decoded != NULL &&
        std::memcmp(decoded, kPixels, sizeof kPixels) == 0);
  CHECK(out.width == 2 && out.height == 2 && out.channels == 4 &&
        out.colorspace == QOI_SRGB);
  CHECK(allocations == 2);

  std::free(decoded);
  std::free(encoded);

  if (failures != 0) {
    std::printf("%d failures\n", failures);
    return 1;
  }
  std::printf("qoi: ok\n");
  return 0;
}
