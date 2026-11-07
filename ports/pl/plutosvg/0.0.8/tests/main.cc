// Consumer test from C++: a document sized against its container (a
// percentage width, the height from the viewBox's aspect ratio), drawn by
// plutosvg_document_render onto the caller's own canvas under the caller's
// transform, with CSS color variables resolved by a lambda or by var()'s
// fallback.
#include <plutosvg.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

namespace {

int failures = 0;

void check(bool ok, const char* what, int line) {
  if (!ok) {
    std::fprintf(stderr, "main.cc:%d: check failed: %s\n", line, what);
    ++failures;
  }
}
#define CHECK(cond) check((cond), #cond, __LINE__)

struct DocumentDeleter {
  void operator()(plutosvg_document_t* d) const { plutosvg_document_destroy(d); }
};
struct SurfaceDeleter {
  void operator()(plutovg_surface_t* s) const { plutovg_surface_destroy(s); }
};
struct CanvasDeleter {
  void operator()(plutovg_canvas_t* c) const { plutovg_canvas_destroy(c); }
};
using Document = std::unique_ptr<plutosvg_document_t, DocumentDeleter>;
using Surface = std::unique_ptr<plutovg_surface_t, SurfaceDeleter>;
using Canvas = std::unique_ptr<plutovg_canvas_t, CanvasDeleter>;

// Premultiplied 0xAARRGGBB at (x, y).
std::uint32_t pixel(const plutovg_surface_t* s, int x, int y) {
  std::uint32_t value;
  std::memcpy(&value,
              plutovg_surface_get_data(s) + y * plutovg_surface_get_stride(s) + 4 * x,
              sizeof value);
  return value;
}

constexpr char svg[] = R"svg(<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" width="50%" viewBox="0 0 4 2">
  <rect width="2" height="2" fill="var(--a)"/>
  <rect x="2" width="2" height="2" fill="var(--b, #00ff00)"/>
</svg>
)svg";

}  // namespace

int main() {
  // Half of a 40-wide container is 20; the 2:1 viewBox makes that 10 high.
  Document document(
      plutosvg_document_load_from_data(svg, -1, 40, -1, nullptr, nullptr));
  CHECK(document != nullptr);
  if (!document) return 1;
  CHECK(plutosvg_document_get_width(document.get()) == 20.f);
  CHECK(plutosvg_document_get_height(document.get()) == 10.f);

  // Twice its size, moved by (4, 2): blue over x 4..24 and green over
  // x 24..44, both over y 2..22.  The palette knows only --a; --b takes
  // its fallback.
  Surface surface(plutovg_surface_create(48, 24));
  Canvas canvas(plutovg_canvas_create(surface.get()));
  plutovg_canvas_translate(canvas.get(), 4, 2);
  plutovg_canvas_scale(canvas.get(), 2, 2);
  auto palette = [](void* closure, const char* name, int length,
                    plutovg_color_t* color) {
    std::string variable(name, length);
    *static_cast<std::string*>(closure) += variable + ';';
    if (variable != "a") return false;
    plutovg_color_init_rgb(color, 0, 0, 1);
    return true;
  };
  std::string asked;
  CHECK(plutosvg_document_render(document.get(), nullptr, canvas.get(),
                                 nullptr, palette, &asked));
  CHECK(asked == "a;b;");
  const plutovg_surface_t* s = surface.get();
  CHECK(pixel(s, 5, 3) == 0xff0000ffu && pixel(s, 23, 21) == 0xff0000ffu);
  CHECK(pixel(s, 24, 2) == 0xff00ff00u && pixel(s, 43, 21) == 0xff00ff00u);
  CHECK(pixel(s, 3, 3) == 0 && pixel(s, 5, 1) == 0);
  CHECK(pixel(s, 44, 12) == 0 && pixel(s, 20, 22) == 0);

  CHECK(!plutosvg_document_render(document.get(), "missing", canvas.get(),
                                  nullptr, nullptr, nullptr));

  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("ok");
  return 0;
}
