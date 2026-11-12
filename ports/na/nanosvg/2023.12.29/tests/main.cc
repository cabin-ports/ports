// Parses an SVG with a linear gradient, a dashed stroke and a transformed
// rect, checks the parsed gradient, stroke and transformed bounds, then
// rasterizes it and checks that the gradient runs from dark to light.
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#define NANOSVG_IMPLEMENTATION
#include <nanosvg.h>
#define NANOSVGRAST_IMPLEMENTATION
#include <nanosvgrast.h>

static int fail(const char* what) {
  std::fprintf(stderr, "%s\n", what);
  return 1;
}

static bool near(float a, float b) { return std::fabs(a - b) < 1e-3f; }

int main() {
  const int w = 40;
  const int h = 10;
  std::string text =
      "<svg width=\"40\" height=\"10\"><defs>"
      "<linearGradient id=\"g\" x1=\"0%\" y1=\"0%\" x2=\"100%\" y2=\"0%\">"
      "<stop offset=\"0\" stop-color=\"#000000\"/>"
      "<stop offset=\"1\" stop-color=\"#ffffff\"/>"
      "</linearGradient></defs>"
      "<rect x=\"0\" y=\"0\" width=\"40\" height=\"10\" fill=\"url(#g)\"/>"
      "<line x1=\"0\" y1=\"5\" x2=\"40\" y2=\"5\" stroke=\"#00ff00\" "
      "stroke-width=\"2\" stroke-dasharray=\"4 2\"/>"
      "<rect x=\"1\" y=\"1\" width=\"2\" height=\"1\" fill=\"blue\" "
      "transform=\"translate(10,2) scale(2)\"/>"
      "</svg>";
  std::vector<char> svg(text.begin(), text.end());
  svg.push_back('\0');  // nsvgParse modifies its input.
  NSVGimage* image = nsvgParse(svg.data(), "px", 96.0f);
  if (image == nullptr) return fail("nsvgParse");

  NSVGshape* fill = image->shapes;
  NSVGshape* line = fill != nullptr ? fill->next : nullptr;
  NSVGshape* moved = line != nullptr ? line->next : nullptr;
  if (fill == nullptr || line == nullptr || moved == nullptr) {
    return fail("shapes");
  }
  if (fill->fill.type != NSVG_PAINT_LINEAR_GRADIENT ||
      fill->fill.gradient->nstops != 2 ||
      fill->fill.gradient->stops[0].color != 0xFF000000u ||
      fill->fill.gradient->stops[1].color != 0xFFFFFFFFu) {
    return fail("gradient");
  }
  if (line->stroke.type != NSVG_PAINT_COLOR ||
      line->stroke.color != 0xFF00FF00u || !near(line->strokeWidth, 2) ||
      line->strokeDashCount != 2 || !near(line->strokeDashArray[0], 4) ||
      !near(line->strokeDashArray[1], 2)) {
    return fail("stroke");
  }
  // (1, 1)-(3, 2), scaled by 2 and then moved by (10, 2).
  if (!near(moved->bounds[0], 12) || !near(moved->bounds[1], 4) ||
      !near(moved->bounds[2], 16) || !near(moved->bounds[3], 6)) {
    return fail("transformed bounds");
  }

  NSVGrasterizer* rast = nsvgCreateRasterizer();
  if (rast == nullptr) return fail("nsvgCreateRasterizer");
  std::vector<unsigned char> img(static_cast<size_t>(w) * h * 4);
  nsvgRasterize(rast, image, 0, 0, 1, img.data(), w, h, w * 4);
  const unsigned char* left = &img[(1 * w + 2) * 4];
  const unsigned char* right = &img[(1 * w + 37) * 4];
  if (left[3] < 250 || right[3] < 250 || left[0] >= 40 || right[0] <= 215 ||
      left[0] != left[1] || right[0] != right[2]) {
    std::fprintf(stderr, "gradient pixels %d and %d\n", left[0], right[0]);
    return 1;
  }

  nsvgDeleteRasterizer(rast);
  nsvgDelete(image);
  std::printf("nanosvg: gradient, stroke, transform and raster ok\n");
  return 0;
}
