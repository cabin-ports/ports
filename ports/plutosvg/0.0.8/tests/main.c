/* The C consumer: an SVG document loaded from memory, its size and element
   extents, and its rendering checked pixel by pixel, with currentColor and
   a CSS color variable resolved through the caller; rendering a single
   element; the destroy callback; a rejected document; and the FreeType
   hooks, which this build (without FreeType) leaves NULL. */
#include <plutosvg.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond)                                                    \
  do {                                                                 \
    if (!(cond)) {                                                     \
      fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, \
              #cond);                                                  \
      exit(1);                                                         \
    }                                                                  \
  } while (0)

static const char svg[] =
    "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"20\" height=\"10\""
    " viewBox=\"0 0 20 10\">"
    "<rect id=\"left\" width=\"10\" height=\"10\" fill=\"#ff0000\"/>"
    "<rect id=\"right\" x=\"10\" width=\"10\" height=\"10\""
    " fill=\"currentColor\" fill-opacity=\"0.5\"/>"
    "<circle id=\"dot\" cx=\"15\" cy=\"5\" r=\"2\" fill=\"var(--accent)\"/>"
    "</svg>";

/* Premultiplied 0xAARRGGBB at (x, y). */
static uint32_t pixel(const plutovg_surface_t *surface, int x, int y) {
  const unsigned char *row = plutovg_surface_get_data(surface) +
                             y * plutovg_surface_get_stride(surface);
  uint32_t value;
  memcpy(&value, row + 4 * x, sizeof value);
  return value;
}

static int channel(uint32_t argb, int shift) {
  return (int)(argb >> shift) & 0xff;
}

static int same_rect(plutovg_rect_t r, float x, float y, float w, float h) {
  return fabsf(r.x - x) < 1e-4f && fabsf(r.y - y) < 1e-4f &&
         fabsf(r.w - w) < 1e-4f && fabsf(r.h - h) < 1e-4f;
}

/* Resolves var(--accent) to green and counts the lookups. */
static bool palette(void *closure, const char *name, int length,
                    plutovg_color_t *color) {
  ++*(int *)closure;
  if (length != 6 || memcmp(name, "accent", 6) != 0) return false;
  plutovg_color_init_rgb(color, 0, 1, 0);
  return true;
}

static void count_destroy(void *closure) { ++*(int *)closure; }

int main(void) {
  CHECK(plutosvg_version() == PLUTOSVG_VERSION &&
        PLUTOSVG_VERSION == PLUTOVG_VERSION_ENCODE(0, 0, 8));
  CHECK(strcmp(plutosvg_version_string(), "0.0.8") == 0);
  CHECK(plutosvg_ft_svg_hooks() == NULL);

  int destroyed = 0;
  plutosvg_document_t *document = plutosvg_document_load_from_data(
      svg, -1, -1, -1, count_destroy, &destroyed);
  CHECK(document != NULL);
  CHECK(plutosvg_document_get_width(document) == 20.f &&
        plutosvg_document_get_height(document) == 10.f);

  plutovg_rect_t extents;
  CHECK(plutosvg_document_extents(document, "left", &extents) &&
        same_rect(extents, 0, 0, 10, 10));
  CHECK(plutosvg_document_extents(document, "dot", &extents) &&
        same_rect(extents, 13, 3, 4, 4));
  CHECK(!plutosvg_document_extents(document, "missing", &extents));

  /* The whole document at its intrinsic size: red on the left, half
     transparent blue (currentColor) on the right, and the circle in the
     palette's green. */
  plutovg_color_t blue;
  plutovg_color_init_rgb(&blue, 0, 0, 1);
  int lookups = 0;
  plutovg_surface_t *surface = plutosvg_document_render_to_surface(
      document, NULL, -1, -1, &blue, palette, &lookups);
  CHECK(surface != NULL);
  CHECK(plutovg_surface_get_width(surface) == 20 &&
        plutovg_surface_get_height(surface) == 10);
  CHECK(lookups == 1);
  CHECK(pixel(surface, 5, 5) == 0xffff0000u);
  uint32_t half = pixel(surface, 11, 1);
  CHECK(channel(half, 24) >= 127 && channel(half, 24) <= 128);
  CHECK(channel(half, 0) == channel(half, 24));
  CHECK(channel(half, 16) == 0 && channel(half, 8) == 0);
  CHECK(pixel(surface, 15, 5) == 0xff00ff00u);
  plutovg_surface_destroy(surface);

  /* One element: the circle's 4x4 box, without the rectangles under it
     (the half-blue one would otherwise cover every pixel). */
  surface = plutosvg_document_render_to_surface(document, "dot", -1, -1, &blue,
                                                palette, &lookups);
  CHECK(surface != NULL);
  CHECK(plutovg_surface_get_width(surface) == 4 &&
        plutovg_surface_get_height(surface) == 4);
  CHECK(pixel(surface, 2, 2) == 0xff00ff00u);
  CHECK(channel(pixel(surface, 0, 0), 24) < 255);
  CHECK(channel(pixel(surface, 0, 0), 16) == 0 &&
        channel(pixel(surface, 0, 0), 0) == 0);
  plutovg_surface_destroy(surface);
  CHECK(plutosvg_document_render_to_surface(document, "missing", -1, -1, NULL,
                                            NULL, NULL) == NULL);

  CHECK(destroyed == 0);
  plutosvg_document_destroy(document);
  CHECK(destroyed == 1);

  /* Not markup at all. */
  CHECK(plutosvg_document_load_from_data("hello", -1, -1, -1, NULL, NULL) ==
        NULL);

  puts("ok");
  return 0;
}
