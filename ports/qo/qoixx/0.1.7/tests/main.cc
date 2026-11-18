#include <qoixx.hpp>

#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char* what, int line) {
  if (!ok) {
    std::fprintf(stderr, "main.cc:%d: check failed: %s\n", line, what);
    ++failures;
  }
}

#define CHECK(...) check((__VA_ARGS__), #__VA_ARGS__, __LINE__)

using bytes = std::vector<std::uint8_t>;
using qoi = qoixx::qoi;

// Encodes to `encoded` and decodes back to `pixels` with the same description.
void round_trip(const bytes& pixels, const qoi::desc& d, const bytes& encoded, int line) {
  check(qoi::encode<bytes>(pixels, d) == encoded, "encode matches", line);
  const auto [decoded, dd] = qoi::decode<bytes>(encoded);
  check(decoded == pixels && dd == d, "decode matches", line);
}

// Upstream's own vector (src/test.cpp): an 8x4 RGB image of 32 distinct pixels, all QOI_OP_RGB.
void test_upstream_vector() {
  const qoi::desc d{.width = 8, .height = 4, .channels = 3, .colorspace = qoi::colorspace::srgb};
  const bytes image = {
      130, 0,   212, 124, 204, 88,  79,  26,  210, 104, 117, 4,   137, 191, 80,  204, 65,  175, 38,  160,
      207, 182, 174, 59,  83,  18,  227, 4,   234, 150, 97,  131, 62,  95,  167, 236, 132, 143, 78,  175,
      86,  172, 237, 113, 195, 87,  227, 242, 13,  189, 125, 33,  16,  79,  165, 247, 216, 193, 192, 113,
      254, 176, 172, 227, 94,  105, 146, 232, 150, 39,  148, 238, 105, 65,  23,  4,   33,  252, 243, 111,
      120, 32,  150, 144, 96,  66,  9,   102, 226, 245, 145, 153, 240, 183, 60,  132};
  const bytes expected = {
      113, 111, 105, 102, 0,   0,   0,   8,   0,   0,   0,   4,   3,   0,   254, 130, 0,   212, 254, 124,
      204, 88,  254, 79,  26,  210, 254, 104, 117, 4,   254, 137, 191, 80,  254, 204, 65,  175, 254, 38,
      160, 207, 254, 182, 174, 59,  254, 83,  18,  227, 254, 4,   234, 150, 254, 97,  131, 62,  254, 95,
      167, 236, 254, 132, 143, 78,  254, 175, 86,  172, 254, 237, 113, 195, 254, 87,  227, 242, 254, 13,
      189, 125, 254, 33,  16,  79,  254, 165, 247, 216, 254, 193, 192, 113, 254, 254, 176, 172, 254, 227,
      94,  105, 254, 146, 232, 150, 254, 39,  148, 238, 254, 105, 65,  23,  254, 4,   33,  252, 254, 243,
      111, 120, 254, 32,  150, 144, 254, 96,  66,  9,   254, 102, 226, 245, 254, 145, 153, 240, 254, 183,
      60,  132, 0,   0,   0,   0,   0,   0,   0,   1};
  round_trip(image, d, expected, __LINE__);
  // The raw pointer and size overloads.
  CHECK(qoi::encode<bytes>(image.data(), image.size(), d) == expected);
  const auto [from_ptr, pd] = qoi::decode<bytes>(expected.data(), expected.size());
  CHECK(from_ptr == image && pd == d);
}

// Byte streams below follow the QOI specification, computed by hand and with a separate encoder.
void test_ops() {
  // RUN (black equals the initial previous pixel), DIFF, RGB, INDEX (hash 4) and LUMA.
  round_trip({0, 0, 0, 1, 1, 1, 11, 21, 9, 1, 1, 1, 6, 10, 12},
             {.width = 5, .height = 1, .channels = 3, .colorspace = qoi::colorspace::srgb},
             {113, 111, 105, 102, 0, 0, 0, 5, 0, 0, 0, 1, 3, 0, 0xC0, 0x7F, 0xFE, 11, 21, 9, 0x04, 0xA9, 0x4A,
              0, 0, 0, 0, 0, 0, 0, 1},
             __LINE__);
  // 70 equal pixels: a run is capped at 62, so QOI_OP_RUN 62 then 8.
  round_trip(bytes(210, 0), {.width = 70, .height = 1, .channels = 3, .colorspace = qoi::colorspace::srgb},
             {113, 111, 105, 102, 0, 0, 0, 70, 0, 0, 0, 1, 3, 0, 0xFD, 0xC7, 0, 0, 0, 0, 0, 0, 0, 1}, __LINE__);
  // An alpha change takes QOI_OP_RGBA; the linear colorspace is carried in the header.
  round_trip({0, 0, 0, 255, 10, 20, 30, 128},
             {.width = 1, .height = 2, .channels = 4, .colorspace = qoi::colorspace::linear},
             {113, 111, 105, 102, 0, 0, 0, 1, 0, 0, 0, 2, 4, 1, 0xC0, 0xFF, 10, 20, 30, 128, 0, 0, 0, 0, 0, 0, 0, 1},
             __LINE__);
  // Decoding RGB data to four channels adds opaque alpha.
  const bytes rgb = qoi::encode<bytes>(bytes{1, 2, 3, 4, 5, 6}, {.width = 2, .height = 1, .channels = 3, .colorspace = qoi::colorspace::srgb});
  const auto [rgba, d] = qoi::decode<bytes>(rgb, 4);
  CHECK(rgba == bytes{1, 2, 3, 255, 4, 5, 6, 255} && d.channels == 3);
}

void test_errors() {
  bool threw = false;
  try {
    (void)qoi::decode<bytes>(bytes(10, 0));
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  CHECK(threw);
  threw = false;
  try {
    // A valid stream with only its magic changed.
    bytes not_qoi = qoi::encode<bytes>(bytes{1, 2, 3}, {.width = 1, .height = 1, .channels = 3, .colorspace = qoi::colorspace::srgb});
    not_qoi[0] = 'x';
    (void)qoi::decode<bytes>(not_qoi);
  } catch (const std::runtime_error&) {
    threw = true;
  }
  CHECK(threw);
  threw = false;
  try {
    // Large enough for 2x1x5, so only the channel count is wrong.
    (void)qoi::encode<bytes>(bytes(10, 0), {.width = 2, .height = 1, .channels = 5, .colorspace = qoi::colorspace::srgb});
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  CHECK(threw);
}

}  // namespace

int main() {
  test_upstream_vector();
  test_ops();
  test_errors();
  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("qoixx 0.1.7 ok");
  return 0;
}
