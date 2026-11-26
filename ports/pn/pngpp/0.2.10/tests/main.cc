// Consumer test: write a 2x2 RGB image to a stream, check the PNG signature,
// read it back through the istream constructor and compare every pixel; the
// same for gray and RGBA images; a non-PNG stream throws png::error.
#include <png++/png.hpp>

#include <cstdio>
#include <sstream>
#include <string>

namespace {

int failures = 0;

void check(bool ok, const char* what) {
  if (!ok) {
    std::printf("FAIL: %s\n", what);
    ++failures;
  }
}

bool has_signature(const std::string& s) {
  return s.size() > 8 && s.compare(0, 8, "\x89PNG\r\n\x1a\n", 8) == 0;
}

bool is_rgb(const png::rgb_pixel& p, int r, int g, int b) {
  return p.red == r && p.green == g && p.blue == b;
}

}  // namespace

int main() {
  {
    png::image<png::rgb_pixel> img(2, 2);
    img.set_pixel(0, 0, png::rgb_pixel(255, 0, 0));
    img.set_pixel(1, 0, png::rgb_pixel(0, 255, 0));
    img.set_pixel(0, 1, png::rgb_pixel(0, 0, 255));
    img.set_pixel(1, 1, png::rgb_pixel(10, 20, 30));
    std::ostringstream out;
    img.write_stream(out);
    const std::string bytes = out.str();
    check(has_signature(bytes), "rgb: PNG signature");

    std::istringstream in(bytes);
    png::image<png::rgb_pixel> back(in);
    check(back.get_width() == 2 && back.get_height() == 2, "rgb: size");
    check(is_rgb(back.get_pixel(0, 0), 255, 0, 0), "rgb: pixel 0,0");
    check(is_rgb(back.get_pixel(1, 0), 0, 255, 0), "rgb: pixel 1,0");
    check(is_rgb(back.get_pixel(0, 1), 0, 0, 255), "rgb: pixel 0,1");
    check(is_rgb(back.get_pixel(1, 1), 10, 20, 30), "rgb: pixel 1,1");
  }
  {
    png::image<png::gray_pixel> img(3, 1);
    img.set_pixel(0, 0, 0);
    img.set_pixel(1, 0, 127);
    img.set_pixel(2, 0, 255);
    std::ostringstream out;
    img.write_stream(out);
    std::istringstream in(out.str());
    png::image<png::gray_pixel> back(in);
    check(back.get_width() == 3 && back.get_height() == 1, "gray: size");
    check(back[0][0] == 0 && back[0][1] == 127 && back[0][2] == 255,
          "gray: pixels");
  }
  {
    png::image<png::rgba_pixel> img(1, 1);
    img.set_pixel(0, 0, png::rgba_pixel(1, 2, 3, 4));
    std::ostringstream out;
    img.write_stream(out);
    std::istringstream in(out.str());
    png::image<png::rgba_pixel> back(in);
    const png::rgba_pixel p = back.get_pixel(0, 0);
    check(p.red == 1 && p.green == 2 && p.blue == 3 && p.alpha == 4,
          "rgba: pixel");
  }
  {
    std::istringstream in("not a png file at all");
    bool threw = false;
    try {
      png::image<png::rgb_pixel> bad(in);
    } catch (const png::error&) {
      threw = true;
    }
    check(threw, "invalid stream throws png::error");
  }
  if (failures == 0) std::printf("OK\n");
  return failures == 0 ? 0 : 1;
}
