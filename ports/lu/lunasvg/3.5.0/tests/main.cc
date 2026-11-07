// Consumer test from C++11: SVG documents parsed, queried, edited and
// rendered through plutovg, then encoded as PNG.  Text is not rendered:
// lunasvg takes its fonts from the system font directories.
#include <lunasvg.h>

#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char* what, int line) {
  if (!ok) {
    std::fprintf(stderr, "main.cc:%d: check failed: %s\n", line, what);
    ++failures;
  }
}
#define CHECK(cond) check((cond), #cond, __LINE__)

// Premultiplied 0xAARRGGBB, as lunasvg renders before convertToRGBA().
std::uint32_t pixel(const lunasvg::Bitmap& bitmap, int x, int y) {
  std::uint32_t value;
  std::memcpy(&value, bitmap.data() + y * bitmap.stride() + 4 * x, sizeof value);
  return value;
}

int channel(std::uint32_t argb, int shift) { return (argb >> shift) & 0xff; }

bool close_to(float a, float b) { return std::fabs(a - b) < 1e-3f; }

void append(void* closure, void* data, int size) {
  const unsigned char* bytes = static_cast<const unsigned char*>(data);
  static_cast<std::vector<unsigned char>*>(closure)->insert(
      static_cast<std::vector<unsigned char>*>(closure)->end(), bytes, bytes + size);
}

const char kSvg[] =
    "<svg xmlns='http://www.w3.org/2000/svg' width='20' height='10'>"
    "<rect id='left' x='0' y='0' width='10' height='10' fill='#ff0000'/>"
    "<rect id='right' x='10' y='0' width='10' height='10' fill='blue'"
    " fill-opacity='0.5'/>"
    "</svg>";

}  // namespace

int main() {
  CHECK(lunasvg_version() == 30500);
  CHECK(std::strcmp(lunasvg_version_string(), "3.5.0") == 0);
  CHECK(lunasvg::Document::loadFromData("not an svg document") == nullptr);

  std::unique_ptr<lunasvg::Document> doc = lunasvg::Document::loadFromData(kSvg);
  CHECK(doc != nullptr);
  if (!doc) return 1;
  CHECK(close_to(doc->width(), 20) && close_to(doc->height(), 10));
  CHECK(doc->querySelectorAll("rect").size() == 2);

  lunasvg::Element left = doc->getElementById("left");
  lunasvg::Element right = doc->getElementById("right");
  CHECK(left && right && !doc->getElementById("missing"));
  CHECK(left.getAttribute("fill") == "#ff0000");
  lunasvg::Box box = right.getBoundingBox();
  CHECK(close_to(box.x, 10) && close_to(box.y, 0) && close_to(box.w, 10) &&
        close_to(box.h, 10));
  CHECK(doc->elementFromPoint(15, 5) == right);
  CHECK(doc->elementFromPoint(5, 5) == left);

  // Opaque red, and half-transparent blue over nothing.
  lunasvg::Bitmap bitmap = doc->renderToBitmap();
  CHECK(bitmap.valid() && bitmap.width() == 20 && bitmap.height() == 10);
  CHECK(pixel(bitmap, 5, 5) == 0xffff0000u);
  std::uint32_t half = pixel(bitmap, 15, 5);
  CHECK(channel(half, 24) >= 127 && channel(half, 24) <= 128);
  CHECK(channel(half, 16) == 0 && channel(half, 8) == 0);
  CHECK(channel(half, 0) == channel(half, 24));

  // The same blue over a white background.
  lunasvg::Bitmap on_white = doc->renderToBitmap(20, 10, 0xffffffffu);
  std::uint32_t blended = pixel(on_white, 15, 5);
  CHECK(channel(blended, 24) == 0xff && channel(blended, 0) == 0xff);
  CHECK(channel(blended, 16) >= 126 && channel(blended, 16) <= 129);

  // Edits reach the next render.  A style sheet overrides the markup's
  // presentation attributes, but not an attribute set through the API,
  // which lunasvg ranks like an !important declaration.
  left.setAttribute("fill", "#00ff00");
  CHECK(pixel(doc->renderToBitmap(), 5, 5) == 0xff00ff00u);
  doc->applyStyleSheet("rect { fill: yellow; fill-opacity: 1 }");
  lunasvg::Bitmap styled = doc->renderToBitmap();
  CHECK(pixel(styled, 5, 5) == 0xff00ff00u);
  CHECK(pixel(styled, 15, 5) == 0xffffff00u);

  // Rendering into a caller's bitmap under a transform.
  lunasvg::Bitmap scaled(40, 20);
  scaled.clear(0x00000000u);
  doc->render(scaled, lunasvg::Matrix::scaled(2, 2));
  CHECK(pixel(scaled, 10, 10) == 0xff00ff00u);
  CHECK(pixel(scaled, 30, 10) == 0xffffff00u);
  CHECK(pixel(scaled, 39, 19) == 0xffffff00u);

  // convertToRGBA() leaves straight R, G, B, A bytes.
  lunasvg::Bitmap rgba = doc->renderToBitmap();
  rgba.convertToRGBA();
  const unsigned char* px = rgba.data() + 5 * rgba.stride() + 4 * 15;
  CHECK(px[0] == 0xff && px[1] == 0xff && px[2] == 0 && px[3] == 0xff);

  // Matrices compose and invert.
  lunasvg::Matrix m = lunasvg::Matrix::translated(10, 20);
  m.scale(2, 3);
  lunasvg::Box mapped = lunasvg::Box(1, 1, 2, 2).transformed(m);
  CHECK(close_to(mapped.x, 12) && close_to(mapped.y, 23) && close_to(mapped.w, 4) &&
        close_to(mapped.h, 6));
  lunasvg::Box back = mapped.transformed(m.inverse());
  CHECK(close_to(back.x, 1) && close_to(back.y, 1) && close_to(back.w, 2) &&
        close_to(back.h, 2));

  // A path, a linear gradient, and a group transform.
  std::unique_ptr<lunasvg::Document> art = lunasvg::Document::loadFromData(
      "<svg xmlns='http://www.w3.org/2000/svg' width='32' height='16'>"
      "<defs><linearGradient id='g'><stop offset='0' stop-color='red'/>"
      "<stop offset='1' stop-color='blue'/></linearGradient></defs>"
      "<path d='M0 0H32V8H0Z' fill='url(#g)'/>"
      "<g transform='translate(0 8)'><path id='tri' d='M0 0L8 8H0Z'/></g>"
      "</svg>");
  CHECK(art != nullptr);
  if (!art) return 1;
  lunasvg::Bitmap gradient = art->renderToBitmap();
  CHECK(channel(pixel(gradient, 0, 4), 16) > 230 && channel(pixel(gradient, 0, 4), 0) < 25);
  CHECK(channel(pixel(gradient, 31, 4), 0) > 230 && channel(pixel(gradient, 31, 4), 16) < 25);
  lunasvg::Box tri = art->getElementById("tri").getGlobalBoundingBox();
  CHECK(close_to(tri.x, 0) && close_to(tri.y, 8) && close_to(tri.w, 8) &&
        close_to(tri.h, 8));
  CHECK(pixel(gradient, 1, 14) == 0xff000000u);  // black, the default fill
  CHECK(pixel(gradient, 6, 9) == 0);             // outside the triangle

  // :first-of-type and :last-of-type where the nearest preceding
  // (following) element sibling has another type: without
  // patches/0001 these loop forever, so a watchdog fails the test.
  {
    std::mutex mutex;
    std::condition_variable finished;
    bool done = false;
    std::thread watchdog([&] {
      std::unique_lock<std::mutex> lock(mutex);
      if (!finished.wait_for(lock, std::chrono::seconds(5), [&] { return done; })) {
        std::fputs("main.cc: :first-of-type/:last-of-type hung for 5s\n", stderr);
        std::_Exit(1);
      }
    });

    const char siblings[] =
        "<svg xmlns='http://www.w3.org/2000/svg' width='30' height='10'>"
        "<rect x='0' y='0' width='10' height='10' fill='black'/>"
        "<path d='M10 0H20V10H10Z' fill='black'/>"
        "<rect x='20' y='0' width='10' height='10' fill='black'/>"
        "</svg>";
    std::unique_ptr<lunasvg::Document> first = lunasvg::Document::loadFromData(siblings);
    std::unique_ptr<lunasvg::Document> last = lunasvg::Document::loadFromData(siblings);
    CHECK(first && last);
    if (first && last) {
      CHECK(first->querySelectorAll("rect:first-of-type").size() == 1);
      CHECK(first->querySelectorAll("rect:last-of-type").size() == 1);
      CHECK(first->querySelectorAll("path:first-of-type").size() == 1);
      CHECK(first->querySelectorAll("path:last-of-type").size() == 1);
      first->applyStyleSheet(":first-of-type { fill: #00ff00 }");
      last->applyStyleSheet(":last-of-type { fill: #00ff00 }");
      lunasvg::Bitmap a = first->renderToBitmap();
      lunasvg::Bitmap b = last->renderToBitmap();
      CHECK(pixel(a, 5, 5) == 0xff00ff00u && pixel(a, 15, 5) == 0xff00ff00u);
      CHECK(pixel(a, 25, 5) == 0xff000000u);  // the second rect
      CHECK(pixel(b, 5, 5) == 0xff000000u);   // the first rect
      CHECK(pixel(b, 15, 5) == 0xff00ff00u && pixel(b, 25, 5) == 0xff00ff00u);
    }

    {
      std::lock_guard<std::mutex> lock(mutex);
      done = true;
    }
    finished.notify_one();
    watchdog.join();
  }

  // PNG through the callback, and the size of the image it holds.
  std::vector<unsigned char> png;
  CHECK(bitmap.writeToPng(append, &png));
  static const unsigned char kSignature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
  CHECK(png.size() > 33 && std::memcmp(png.data(), kSignature, 8) == 0);
  CHECK(png.size() > 33 && png[19] == 20 && png[23] == 10);  // IHDR width, height

  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("lunasvg consumer test passed");
  return 0;
}
