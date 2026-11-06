/* Consumer test: rasterized fills, blending, fill rules, strokes, clips and
   gradients checked pixel by pixel; path parsing, measuring and dashing;
   matrices; CSS colors; a PNG written to a stream and read back; UTF-8
   decoding; and the font loaders' failure paths. */
#include <plutovg.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int failures = 0;

static void check(int ok, const char *what, int line)
{
  if (!ok) {
    fprintf(stderr, "main.c:%d: check failed: %s\n", line, what);
    ++failures;
  }
}
#define CHECK(cond) check((cond) ? 1 : 0, #cond, __LINE__)

static int close_to(float a, float b) { return fabsf(a - b) < 1e-3f; }

/* Premultiplied 0xAARRGGBB at (x, y). */
static uint32_t pixel(const plutovg_surface_t *surface, int x, int y)
{
  const unsigned char *row = plutovg_surface_get_data(surface) +
                             y * plutovg_surface_get_stride(surface);
  uint32_t value;
  memcpy(&value, row + 4 * x, sizeof value);
  return value;
}

static int channel(uint32_t argb, int shift) { return (int)(argb >> shift) & 0xff; }

static void test_version(void)
{
  CHECK(plutovg_version() == PLUTOVG_VERSION && PLUTOVG_VERSION == 10303);
  CHECK(strcmp(plutovg_version_string(), "1.3.3") == 0);
}

static void test_fill_and_blend(void)
{
  plutovg_surface_t *surface = plutovg_surface_create(16, 16);
  plutovg_canvas_t *canvas = plutovg_canvas_create(surface);
  uint32_t mixed;
  CHECK(plutovg_surface_get_width(surface) == 16);
  CHECK(plutovg_surface_get_stride(surface) >= 16 * 4);
  CHECK(plutovg_surface_get_reference_count(surface) == 2); /* the canvas */
  CHECK(pixel(surface, 8, 8) == 0); /* surfaces start transparent */

  plutovg_canvas_set_rgb(canvas, 1, 0, 0);
  plutovg_canvas_fill_rect(canvas, 2, 2, 4, 4);
  CHECK(pixel(surface, 2, 2) == 0xffff0000u);
  CHECK(pixel(surface, 5, 5) == 0xffff0000u);
  CHECK(pixel(surface, 6, 6) == 0);
  CHECK(pixel(surface, 1, 3) == 0);

  /* Half-transparent blue over the red: opaque, half red, half blue. */
  plutovg_canvas_set_rgba(canvas, 0, 0, 1, 0.5f);
  plutovg_canvas_fill_rect(canvas, 4, 4, 4, 4);
  mixed = pixel(surface, 4, 4);
  CHECK(channel(mixed, 24) == 0xff);
  CHECK(channel(mixed, 16) >= 126 && channel(mixed, 16) <= 129);
  CHECK(channel(mixed, 8) == 0);
  CHECK(channel(mixed, 0) >= 126 && channel(mixed, 0) <= 129);
  /* Over transparency it stays premultiplied half-alpha blue. */
  mixed = pixel(surface, 7, 7);
  CHECK(channel(mixed, 24) >= 127 && channel(mixed, 24) <= 128);
  CHECK(channel(mixed, 0) == channel(mixed, 24) && channel(mixed, 16) == 0);

  plutovg_canvas_destroy(canvas);
  CHECK(plutovg_surface_get_reference_count(surface) == 1);
  plutovg_surface_destroy(surface);
}

static void test_fill_rules_strokes_and_clips(void)
{
  plutovg_surface_t *surface = plutovg_surface_create(16, 16);
  plutovg_canvas_t *canvas = plutovg_canvas_create(surface);
  plutovg_rect_t extents;
  plutovg_color_t transparent;

  /* Two nested squares wound the same way. */
  plutovg_canvas_rect(canvas, 0, 0, 16, 16);
  plutovg_canvas_rect(canvas, 4, 4, 8, 8);
  CHECK(plutovg_canvas_fill_contains(canvas, 8, 8));
  plutovg_canvas_set_fill_rule(canvas, PLUTOVG_FILL_RULE_EVEN_ODD);
  CHECK(!plutovg_canvas_fill_contains(canvas, 8, 8));
  CHECK(plutovg_canvas_fill_contains(canvas, 2, 2));
  plutovg_canvas_set_rgb(canvas, 0, 1, 0);
  plutovg_canvas_fill(canvas);
  CHECK(pixel(surface, 2, 2) == 0xff00ff00u);
  CHECK(pixel(surface, 8, 8) == 0);

  /* A 2-pixel stroke centred on the hole's edge covers x 3-4 only.
     plutovg_canvas_stroke_contains is not checked: 1.3.3 tests the path as
     a fill there (source/plutovg-canvas.c:513). */
  plutovg_canvas_rect(canvas, 4, 4, 8, 8);
  plutovg_canvas_set_line_width(canvas, 2);
  plutovg_canvas_stroke_extents(canvas, &extents);
  CHECK(close_to(extents.x, 3) && close_to(extents.y, 3));
  CHECK(close_to(extents.w, 10) && close_to(extents.h, 10));
  plutovg_canvas_set_rgb(canvas, 0, 0, 1);
  plutovg_canvas_stroke(canvas);
  CHECK(pixel(surface, 4, 8) == 0xff0000ffu);
  CHECK(pixel(surface, 3, 8) == 0xff0000ffu);
  CHECK(pixel(surface, 5, 8) == 0);
  CHECK(pixel(surface, 8, 8) == 0);

  /* Clipped to the left half, a full fill paints only that half. */
  plutovg_color_init_rgba(&transparent, 0, 0, 0, 0);
  plutovg_surface_clear(surface, &transparent);
  CHECK(pixel(surface, 2, 2) == 0);
  plutovg_canvas_save(canvas);
  plutovg_canvas_clip_rect(canvas, 0, 0, 8, 16);
  plutovg_canvas_set_rgb(canvas, 1, 1, 1);
  plutovg_canvas_paint(canvas);
  plutovg_canvas_restore(canvas);
  CHECK(pixel(surface, 4, 4) == 0xffffffffu);
  CHECK(pixel(surface, 12, 4) == 0);

  plutovg_canvas_destroy(canvas);
  plutovg_surface_destroy(surface);
}

static void test_gradient(void)
{
  plutovg_gradient_stop_t stops[2];
  plutovg_surface_t *surface = plutovg_surface_create(16, 1);
  plutovg_canvas_t *canvas = plutovg_canvas_create(surface);
  uint32_t left, right;
  stops[0].offset = 0;
  plutovg_color_init_rgb(&stops[0].color, 1, 0, 0);
  stops[1].offset = 1;
  plutovg_color_init_rgb(&stops[1].color, 0, 0, 1);
  plutovg_canvas_set_linear_gradient(canvas, 0, 0, 16, 0,
                                     PLUTOVG_SPREAD_METHOD_PAD, stops, 2, NULL);
  plutovg_canvas_paint(canvas);
  left = pixel(surface, 0, 0);
  right = pixel(surface, 15, 0);
  CHECK(channel(left, 24) == 0xff && channel(right, 24) == 0xff);
  CHECK(channel(left, 16) > 230 && channel(left, 0) < 25);
  CHECK(channel(right, 0) > 230 && channel(right, 16) < 25);
  plutovg_canvas_destroy(canvas);
  plutovg_surface_destroy(surface);
}

static void test_paths(void)
{
  static const float dashes[] = {2, 2};
  plutovg_path_t *path = plutovg_path_create();
  plutovg_path_t *line, *dashed;
  plutovg_path_iterator_t it;
  plutovg_point_t points[3];
  plutovg_rect_t extents;
  int commands[8], count = 0;

  CHECK(plutovg_path_parse(path, "M10 10 L30 10 L30 30 Z", -1));
  plutovg_path_iterator_init(&it, path);
  while (plutovg_path_iterator_has_next(&it) && count < 8)
    commands[count++] = plutovg_path_iterator_next(&it, points);
  CHECK(count == 4);
  CHECK(commands[0] == PLUTOVG_PATH_COMMAND_MOVE_TO &&
        commands[1] == PLUTOVG_PATH_COMMAND_LINE_TO &&
        commands[2] == PLUTOVG_PATH_COMMAND_LINE_TO &&
        commands[3] == PLUTOVG_PATH_COMMAND_CLOSE);
  plutovg_path_extents(path, &extents, true);
  CHECK(close_to(extents.x, 10) && close_to(extents.y, 10));
  CHECK(close_to(extents.w, 20) && close_to(extents.h, 20));
  CHECK(close_to(plutovg_path_length(path), 40 + 20 * sqrtf(2)));
  CHECK(!plutovg_path_parse(path, "M10 10 L", -1));

  line = plutovg_path_create();
  plutovg_path_move_to(line, 0, 0);
  plutovg_path_line_to(line, 10, 0);
  dashed = plutovg_path_clone_dashed(line, 0, dashes, 2);
  CHECK(close_to(plutovg_path_length(dashed), 6)); /* 0-2, 4-6, 8-10 */
  plutovg_path_destroy(dashed);
  plutovg_path_destroy(line);
  plutovg_path_destroy(path);
}

static void test_matrices(void)
{
  plutovg_matrix_t m, parsed, inverse;
  float x, y;
  plutovg_matrix_init_translate(&m, 10, 20);
  plutovg_matrix_scale(&m, 2, 3);
  plutovg_matrix_map(&m, 1, 1, &x, &y);
  CHECK(close_to(x, 12) && close_to(y, 23));
  CHECK(plutovg_matrix_invert(&m, &inverse));
  plutovg_matrix_map(&inverse, 12, 23, &x, &y);
  CHECK(close_to(x, 1) && close_to(y, 1));
  CHECK(plutovg_matrix_parse(&parsed, "translate(10, 20) scale(2, 3)", -1));
  CHECK(close_to(parsed.a, m.a) && close_to(parsed.d, m.d) && close_to(parsed.e, m.e) &&
        close_to(parsed.f, m.f) && close_to(parsed.b, 0) && close_to(parsed.c, 0));
  plutovg_matrix_init_rotate(&m, PLUTOVG_DEG2RAD(90));
  plutovg_matrix_map(&m, 1, 0, &x, &y);
  CHECK(close_to(x, 0) && close_to(y, 1));
  plutovg_matrix_init_scale(&m, 0, 1);
  CHECK(!plutovg_matrix_invert(&m, &inverse));
}

static void test_colors(void)
{
  plutovg_color_t color;
  CHECK(plutovg_color_parse(&color, "#ff8000", -1) == 7);
  CHECK(plutovg_color_to_rgba32(&color) == 0xff8000ffu);
  CHECK(plutovg_color_parse(&color, "rgb(0, 128, 255)", -1) > 0);
  CHECK(plutovg_color_to_rgba32(&color) == 0x0080ffffu);
  CHECK(plutovg_color_parse(&color, "hsl(120, 100%, 50%)", -1) > 0);
  CHECK(plutovg_color_to_rgba32(&color) == 0x00ff00ffu);
  CHECK(plutovg_color_parse(&color, "red", -1) == 3);
  CHECK(plutovg_color_to_argb32(&color) == 0xffff0000u);
  CHECK(plutovg_color_parse(&color, "nonsense", -1) == 0);
  plutovg_color_init_rgba8(&color, 1, 2, 3, 4);
  CHECK(plutovg_color_to_argb32(&color) == 0x04010203u);
}

typedef struct {
  unsigned char data[2048];
  int size;
  int overflow;
} buffer;

static void write_to(void *closure, void *data, int size)
{
  buffer *b = (buffer *)closure;
  if (b->size + size > (int)sizeof b->data) {
    b->overflow = 1;
    return;
  }
  memcpy(b->data + b->size, data, (size_t)size);
  b->size += size;
}

static void test_png_round_trip(void)
{
  static buffer png;
  plutovg_surface_t *surface = plutovg_surface_create(8, 8);
  plutovg_canvas_t *canvas = plutovg_canvas_create(surface);
  plutovg_surface_t *loaded;
  int x, y, same = 1;
  plutovg_canvas_set_rgb(canvas, 0.2f, 0.4f, 0.6f);
  plutovg_canvas_fill_rect(canvas, 2, 0, 4, 8);
  CHECK(plutovg_surface_write_to_png_stream(surface, write_to, &png));
  CHECK(!png.overflow && png.size > 8);
  CHECK(memcmp(png.data, "\x89PNG\r\n\x1a\n", 8) == 0);
  loaded = plutovg_surface_load_from_image_data(png.data, png.size);
  CHECK(loaded != NULL);
  if (loaded != NULL) {
    CHECK(plutovg_surface_get_width(loaded) == 8 &&
          plutovg_surface_get_height(loaded) == 8);
    for (y = 0; y < 8; ++y)
      for (x = 0; x < 8; ++x)
        same &= pixel(loaded, x, y) == pixel(surface, x, y);
    CHECK(same);
    CHECK(pixel(loaded, 3, 3) == 0xff336699u);
    plutovg_surface_destroy(loaded);
  }
  CHECK(plutovg_surface_load_from_image_data("not an image", 12) == NULL);
  plutovg_canvas_destroy(canvas);
  plutovg_surface_destroy(surface);
}

static void test_text_iterator(void)
{
  /* "a", U+00E9, U+20AC, U+1F600 in UTF-8. */
  static const char text[] = "a\xc3\xa9\xe2\x82\xac\xf0\x9f\x98\x80";
  static const plutovg_codepoint_t expected[] = {0x61, 0xe9, 0x20ac, 0x1f600};
  plutovg_text_iterator_t it;
  int count = 0;
  plutovg_text_iterator_init(&it, text, -1, PLUTOVG_TEXT_ENCODING_UTF8);
  while (plutovg_text_iterator_has_next(&it)) {
    plutovg_codepoint_t c = plutovg_text_iterator_next(&it);
    if (count < 4) CHECK(c == expected[count]);
    ++count;
  }
  CHECK(count == 4);
}

static int destroyed = 0;
static void count_destroy(void *closure)
{
  (void)closure;
  ++destroyed;
}

static void test_font_failures(void)
{
  static const char junk[] = "definitely not a font file";
  plutovg_font_face_cache_t *cache = plutovg_font_face_cache_create();
  /* A rejected face releases its data through the destroy callback. */
  CHECK(plutovg_font_face_load_from_data(junk, sizeof junk, 0, count_destroy,
                                         NULL) == NULL);
  CHECK(destroyed == 1);
  /* 0 faces, not the -1 of a build without cache loading. */
  CHECK(plutovg_font_face_cache_load_file(cache, "no/such/font.ttf") == 0);
  CHECK(plutovg_font_face_cache_load_dir(cache, "no/such/dir") == 0);
  CHECK(plutovg_font_face_cache_get(cache, "Sans", false, false) == NULL);
  plutovg_font_face_cache_destroy(cache);
}

int main(void)
{
  test_version();
  test_fill_and_blend();
  test_fill_rules_strokes_and_clips();
  test_gradient();
  test_paths();
  test_matrices();
  test_colors();
  test_png_round_trip();
  test_text_iterator();
  test_font_failures();
  if (failures != 0) {
    fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  puts("plutovg consumer test passed");
  return 0;
}
