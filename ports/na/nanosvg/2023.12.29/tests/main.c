/* Parses an SVG with a rect and an arc path, checks the image size, colours
   and the arc's bounds, duplicates the path, then rasterizes the image and
   checks pixels inside each shape and outside both. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NANOSVG_IMPLEMENTATION
#include <nanosvg.h>
#define NANOSVGRAST_IMPLEMENTATION
#include <nanosvgrast.h>

#define W 20
#define H 10

static int fail(const char *what) {
  fprintf(stderr, "%s\n", what);
  return 1;
}

static int near(float a, float b) { return fabsf(a - b) < 1e-3f; }

static const unsigned char *pixel(const unsigned char *img, int x, int y) {
  return img + (y * W + x) * 4;
}

int main(void) {
  /* nsvgParse modifies its input. */
  char svg[] =
      "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"20\" height=\"10\">"
      "<rect x=\"0\" y=\"0\" width=\"10\" height=\"10\" fill=\"#ff0000\"/>"
      "<path d=\"M 10 10 A 5 5 0 0 1 20 10 Z\" fill=\"rgb(0,0,255)\"/>"
      "</svg>";
  NSVGimage *image = nsvgParse(svg, "px", 96.0f);
  NSVGshape *rect, *arc;
  NSVGpath *dup;
  NSVGrasterizer *rast;
  unsigned char *img;
  const unsigned char *p;

  if (image == NULL) return fail("nsvgParse");
  if (image->width != 20.0f || image->height != 10.0f) return fail("size");
  rect = image->shapes;
  arc = rect != NULL ? rect->next : NULL;
  if (rect == NULL || arc == NULL || arc->next != NULL) return fail("shapes");
  if (rect->fill.type != NSVG_PAINT_COLOR || rect->fill.color != 0xFF0000FFu ||
      arc->fill.type != NSVG_PAINT_COLOR || arc->fill.color != 0xFFFF0000u) {
    return fail("fill colours");
  }
  /* The upper half of a radius-5 circle centred at (15, 10). */
  if (!near(arc->bounds[0], 10) || !near(arc->bounds[1], 5) ||
      !near(arc->bounds[2], 20) || !near(arc->bounds[3], 10)) {
    fprintf(stderr, "arc bounds %g %g %g %g\n", arc->bounds[0],
            arc->bounds[1], arc->bounds[2], arc->bounds[3]);
    return 1;
  }

  dup = nsvgDuplicatePath(arc->paths);
  if (dup == NULL || dup->npts != arc->paths->npts ||
      memcmp(dup->pts, arc->paths->pts,
             sizeof(float) * 2 * (size_t)dup->npts) != 0) {
    return fail("nsvgDuplicatePath");
  }
  free(dup->pts);
  free(dup);

  rast = nsvgCreateRasterizer();
  img = calloc(W * H, 4);
  if (rast == NULL || img == NULL) return fail("allocation");
  nsvgRasterize(rast, image, 0, 0, 1, img, W, H, W * 4);
  p = pixel(img, 2, 5);
  if (p[0] != 255 || p[1] != 0 || p[2] != 0 || p[3] < 250) {
    return fail("pixel in rect");
  }
  p = pixel(img, 15, 8);
  if (p[0] != 0 || p[1] != 0 || p[2] != 255 || p[3] < 250) {
    return fail("pixel in arc");
  }
  if (pixel(img, 11, 1)[3] != 0) return fail("pixel outside");

  free(img);
  nsvgDeleteRasterizer(rast);
  nsvgDelete(image);
  printf("nanosvg: parse, arc bounds and rasterization ok\n");
  return 0;
}
