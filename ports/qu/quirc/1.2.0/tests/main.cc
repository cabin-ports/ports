// quirc.h from C++ (its declarations are extern "C"): the ISO/IEC 18004
// Annex I symbol, "01234567" as version 1-M with mask 2, built by an
// independent encoder, decodes from its cell bitmap and from a rendered
// image.
#include <quirc.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, int line, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "main.cc:%d: check failed: %s\n", line, what);
    ++failures;
  }
}

#define CHECK(expr) check((expr), __LINE__, #expr)

const int kSize = 21;

const char* const kAnnexI[kSize] = {
    "#######..#.##.#######", "#.....#..####.#.....#", "#.###.#.#.....#.###.#",
    "#.###.#.##....#.###.#", "#.###.#.#.###.#.###.#", "#.....#.#...#.#.....#",
    "#######.#.#.#.#######", "........#..##........", "#.#####..#..#.#####..",
    "...#.#.##.#.#..#.##..", "..#...##.#.#.#..#####", "....#....#.....####..",
    "...######..#.#..#....", "........#.#####..##..", "#######..##.#.##.....",
    "#.....#.#.#####...#.#", "#.###.#.#...#..#.##..", "#.###.#.##..#..#.....",
    "#.###.#.#.##.#..#.#..", "#.....#........##.##.", "#######.####.#..#.#..",
};

std::string payload(const quirc_data& data) {
  return std::string(reinterpret_cast<const char*>(data.payload),
                     static_cast<std::size_t>(data.payload_len));
}

}  // namespace

int main() {
  std::unique_ptr<quirc_code> code(new quirc_code());
  code->size = kSize;
  for (int y = 0; y < kSize; ++y)
    for (int x = 0; x < kSize; ++x)
      if (kAnnexI[y][x] == '#') {
        int i = y * kSize + x;
        code->cell_bitmap[i >> 3] |= static_cast<std::uint8_t>(1u << (i & 7));
      }
  std::unique_ptr<quirc_data> data(new quirc_data());
  CHECK(quirc_decode(code.get(), data.get()) == QUIRC_SUCCESS);
  CHECK(data->ecc_level == QUIRC_ECC_LEVEL_M && data->mask == 2);
  CHECK(payload(*data) == "01234567");

  // Rendered at 3 pixels per module with a 4-module quiet zone.
  const int scale = 3, quiet = 4, side = (kSize + 2 * quiet) * scale;
  std::unique_ptr<quirc, void (*)(quirc*)> q(quirc_new(), quirc_destroy);
  const bool ready = q && quirc_resize(q.get(), side, side) == 0;
  CHECK(ready);
  if (ready) {
    int w = 0, h = 0;
    std::uint8_t* image = quirc_begin(q.get(), &w, &h);
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(w * h), 255);
    for (int y = 0; y < kSize; ++y)
      for (int x = 0; x < kSize; ++x)
        if (kAnnexI[y][x] == '#')
          for (int py = 0; py < scale; ++py)
            for (int px = 0; px < scale; ++px)
              pixels[((quiet + y) * scale + py) * w + (quiet + x) * scale +
                     px] = 0;
    std::memcpy(image, pixels.data(), pixels.size());
    quirc_end(q.get());
    CHECK(quirc_count(q.get()) == 1);
    if (quirc_count(q.get()) == 1) {
      quirc_extract(q.get(), 0, code.get());
      *data = quirc_data();
      CHECK(quirc_decode(code.get(), data.get()) == QUIRC_SUCCESS);
      CHECK(payload(*data) == "01234567");
    }
  }

  if (failures != 0) return 1;
  std::puts("quirc 1.2.0: C++ API OK");
  return 0;
}
