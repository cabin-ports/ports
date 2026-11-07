// Consumer test from C++: plutovg objects held by std::unique_ptr, a circle
// rasterized and measured, a path walked through its traverse callback,
// SVG path data with an arc, and UTF-16 text decoding.
#include <plutovg.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
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

struct SurfaceDeleter {
  void operator()(plutovg_surface_t* s) const { plutovg_surface_destroy(s); }
};
struct CanvasDeleter {
  void operator()(plutovg_canvas_t* c) const { plutovg_canvas_destroy(c); }
};
struct PathDeleter {
  void operator()(plutovg_path_t* p) const { plutovg_path_destroy(p); }
};
using Surface = std::unique_ptr<plutovg_surface_t, SurfaceDeleter>;
using Canvas = std::unique_ptr<plutovg_canvas_t, CanvasDeleter>;
using Path = std::unique_ptr<plutovg_path_t, PathDeleter>;

std::uint32_t pixel(const plutovg_surface_t* s, int x, int y) {
  std::uint32_t value;
  std::memcpy(&value,
              plutovg_surface_get_data(s) + y * plutovg_surface_get_stride(s) + 4 * x,
              sizeof value);
  return value;
}

void record(void* closure, plutovg_path_command_t command,
            const plutovg_point_t*, int) {
  static_cast<std::vector<plutovg_path_command_t>*>(closure)->push_back(command);
}

bool close_to(float a, float b, float tolerance = 1e-3f) {
  return std::fabs(a - b) < tolerance;
}

}  // namespace

int main() {
  // A filled circle of radius 6: 88 pixels lie wholly inside it, 132 touch
  // it, and the antialiased coverage adds up to about its area, 36 pi
  // (110.4 of 113.1 with Apple Clang on arm64).
  Surface surface(plutovg_surface_create(16, 16));
  Canvas canvas(plutovg_canvas_create(surface.get()));
  plutovg_canvas_set_rgb(canvas.get(), 1, 1, 1);
  plutovg_canvas_circle(canvas.get(), 8, 8, 6);
  plutovg_canvas_fill(canvas.get());
  int opaque = 0, touched = 0;
  float coverage = 0;
  for (int y = 0; y < 16; ++y)
    for (int x = 0; x < 16; ++x) {
      std::uint32_t a = pixel(surface.get(), x, y) >> 24;
      opaque += a == 0xff;
      touched += a != 0;
      coverage += a / 255.0f;
    }
  CHECK(pixel(surface.get(), 8, 8) == 0xffffffffu);
  CHECK(pixel(surface.get(), 0, 0) == 0);
  CHECK(opaque >= 88 && opaque < touched && touched <= 132);
  CHECK(close_to(coverage, 36 * PLUTOVG_PI, 4.0f));

  // add_circle is a move, four cubic quarter arcs, and a close.
  Path circle(plutovg_path_create());
  plutovg_path_add_circle(circle.get(), 0, 0, 1);
  std::vector<plutovg_path_command_t> commands;
  plutovg_path_traverse(circle.get(), record, &commands);
  CHECK(commands.size() == 6);
  if (commands.size() == 6) {
    CHECK(commands.front() == PLUTOVG_PATH_COMMAND_MOVE_TO);
    CHECK(commands.back() == PLUTOVG_PATH_COMMAND_CLOSE);
    for (std::size_t i = 1; i < 5; ++i)
      CHECK(commands[i] == PLUTOVG_PATH_COMMAND_CUBIC_TO);
  }
  CHECK(close_to(plutovg_path_length(circle.get()), 2 * PLUTOVG_PI, 0.01f));

  // A half circle of radius 10 from (0, 10) to (0, -10), bulging right.
  Path arc(plutovg_path_create());
  CHECK(plutovg_path_parse(arc.get(), "M0 10 A10 10 0 0 0 0 -10", -1));
  plutovg_rect_t box;
  plutovg_path_extents(arc.get(), &box, true);
  CHECK(close_to(box.y, -10) && close_to(box.h, 20));
  CHECK(close_to(box.x, 0) && close_to(box.w, 10, 0.01f));

  // UTF-16 within the BMP (1.3.3 does not join surrogate pairs) and UTF-32.
  const char16_t utf16[] = u"A€ж";
  const char32_t utf32[] = U"\U0001F600z";
  std::vector<plutovg_codepoint_t> codepoints;
  plutovg_text_iterator_t it;
  plutovg_text_iterator_init(&it, utf16, -1, PLUTOVG_TEXT_ENCODING_UTF16);
  while (plutovg_text_iterator_has_next(&it))
    codepoints.push_back(plutovg_text_iterator_next(&it));
  plutovg_text_iterator_init(&it, utf32, -1, PLUTOVG_TEXT_ENCODING_UTF32);
  while (plutovg_text_iterator_has_next(&it))
    codepoints.push_back(plutovg_text_iterator_next(&it));
  const std::vector<plutovg_codepoint_t> expected = {0x41, 0x20ac, 0x436,
                                                     0x1f600, 0x7a};
  CHECK(codepoints == expected);

  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("plutovg C++ consumer test passed");
  return 0;
}
