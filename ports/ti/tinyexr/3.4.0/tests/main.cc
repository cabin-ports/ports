// The C++ consumer: the legacy API writes an RGBA float image to memory
// (ZIP by default, through miniz), reads it back exactly, and rejects damaged
// input.  Save and load follow the SaveEXRToMemory and LoadEXRFromMemory
// documentation in tinyexr.h.
#include <tinyexr.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#define CHECK(cond)                                                       \
  do {                                                                    \
    if (!(cond)) {                                                        \
      std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__,         \
                   __LINE__, #cond);                                      \
      std::exit(1);                                                       \
    }                                                                     \
  } while (0)

static const int W = 32;
static const int H = 16;

static std::vector<float> make_image() {
  std::vector<float> rgba(static_cast<size_t>(W) * H * 4);
  for (int y = 0; y < H; ++y)
    for (int x = 0; x < W; ++x) {
      float *p = &rgba[(static_cast<size_t>(y) * W + x) * 4];
      p[0] = static_cast<float>(x);
      p[1] = static_cast<float>(y);
      p[2] = 0.25f * static_cast<float>(x + y);
      p[3] = 1.0f;
    }
  return rgba;
}

static void check_roundtrip(const unsigned char *data, size_t size,
                            const std::vector<float> &want) {
  float *out = NULL;
  int w = 0, h = 0;
  const char *err = NULL;
  CHECK(LoadEXRFromMemory(&out, &w, &h, data, size, &err) == TINYEXR_SUCCESS);
  CHECK(w == W && h == H);
  CHECK(std::memcmp(out, want.data(), want.size() * sizeof(float)) == 0);
  std::free(out);
}

int main() {
  const std::vector<float> rgba = make_image();
  unsigned char *buf = NULL;
  const char *err = NULL;

  // Float save: ZIP, smaller than the raw pixels, exact on reading back.
  int size = SaveEXRToMemory(rgba.data(), W, H, 4, 0, &buf, &err);
  CHECK(size > 0);
  CHECK(static_cast<size_t>(size) < rgba.size() * sizeof(float));
  {
    EXRVersion version;
    EXRHeader header;
    InitEXRHeader(&header);
    CHECK(ParseEXRVersionFromMemory(&version, buf, size) == TINYEXR_SUCCESS);
    CHECK(ParseEXRHeaderFromMemory(&header, &version, buf, size, &err) ==
          TINYEXR_SUCCESS);
    CHECK(header.compression_type == TINYEXR_COMPRESSIONTYPE_ZIP);
    CHECK(header.num_channels == 4);
    FreeEXRHeader(&header);
  }
  check_roundtrip(buf, size, rgba);

  // Half save: these values are exact in half precision.
  unsigned char *half = NULL;
  int half_size = SaveEXRToMemory(rgba.data(), W, H, 4, 1, &half, &err);
  CHECK(half_size > 0);
  CHECK(half_size < size);
  check_roundtrip(half, half_size, rgba);
  std::free(half);

  // Damaged input: a bad magic number, a truncated stream, and an
  // unsupported component count are errors with a message.
  std::vector<unsigned char> bad(buf, buf + size);
  bad[0] ^= 0xff;
  float *out = NULL;
  int w = 0, h = 0;
  err = NULL;
  CHECK(LoadEXRFromMemory(&out, &w, &h, bad.data(), bad.size(), &err) !=
        TINYEXR_SUCCESS);
  CHECK(err != NULL);
  FreeEXRErrorMessage(err);
  err = NULL;
  CHECK(LoadEXRFromMemory(&out, &w, &h, buf, size / 2, &err) !=
        TINYEXR_SUCCESS);
  CHECK(err != NULL);
  FreeEXRErrorMessage(err);
  unsigned char *none = NULL;
  err = NULL;
  CHECK(SaveEXRToMemory(rgba.data(), W, H, 2, 0, &none, &err) < 0);
  CHECK(none == NULL);
  FreeEXRErrorMessage(err);

  std::free(buf);
  std::puts("ok");
  return 0;
}
