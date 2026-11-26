#include <nanovg.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(cond)                                                    \
  do {                                                                 \
    if (!(cond)) {                                                     \
      printf("%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond);  \
      failures++;                                                      \
    }                                                                  \
  } while (0)

#define NEAR(a, b) (fabsf((a) - (b)) < 1e-4f)

/* 2x2 RGBA PNG: red, green / blue, white at alpha 128 (built with Python's
   zlib). */
static const unsigned char kPng[76] = {
  0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d,
  0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02,
  0x08, 0x06, 0x00, 0x00, 0x00, 0x72, 0xb6, 0x0d, 0x24, 0x00, 0x00, 0x00,
  0x13, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9c, 0x63, 0xf8, 0xcf, 0xc0, 0xf0,
  0x1f, 0x0c, 0x81, 0x34, 0x08, 0x34, 0x00, 0x00, 0x49, 0x49, 0x09, 0x78,
  0x28, 0xa0, 0xdb, 0x77, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44,
  0xae, 0x42, 0x60, 0x82,
};

/* A recording back-end in place of the OpenGL ones. */
static struct {
  int textures, last_w, last_h, last_type;
  unsigned char last_pixels[16];
  int fills, fill_paths, fill_nfill, fill_convex, flushes;
  float fill_bounds[4];
  int deleted;
} rec;

static int r_create(void *u) { (void)u; return 1; }
static int r_create_tex(void *u, int type, int w, int h, int flags,
                        const unsigned char *data) {
  (void)u; (void)flags;
  rec.last_type = type;
  rec.last_w = w;
  rec.last_h = h;
  if (data && w * h * 4 <= (int)sizeof rec.last_pixels)
    memcpy(rec.last_pixels, data, (size_t)(w * h * 4));
  return ++rec.textures;
}
static int r_delete_tex(void *u, int id) { (void)u; (void)id; return 1; }
static int r_update_tex(void *u, int id, int x, int y, int w, int h,
                        const unsigned char *d) {
  (void)u; (void)id; (void)x; (void)y; (void)w; (void)h; (void)d;
  return 1;
}
static int r_tex_size(void *u, int id, int *w, int *h) {
  (void)u; (void)id;
  *w = rec.last_w;
  *h = rec.last_h;
  return 1;
}
static void r_viewport(void *u, float w, float h, float r) {
  (void)u; (void)w; (void)h; (void)r;
}
static void r_cancel(void *u) { (void)u; }
static void r_flush(void *u) { (void)u; rec.flushes++; }
static void r_fill(void *u, NVGpaint *p, NVGcompositeOperationState op,
                   NVGscissor *s, float fringe, const float *bounds,
                   const NVGpath *paths, int npaths) {
  (void)u; (void)p; (void)op; (void)s; (void)fringe;
  rec.fills++;
  rec.fill_paths = npaths;
  rec.fill_nfill = paths[0].nfill;
  rec.fill_convex = paths[0].convex;
  memcpy(rec.fill_bounds, bounds, sizeof rec.fill_bounds);
}
static void r_stroke(void *u, NVGpaint *p, NVGcompositeOperationState op,
                     NVGscissor *s, float fringe, float w,
                     const NVGpath *paths, int npaths) {
  (void)u; (void)p; (void)op; (void)s; (void)fringe; (void)w; (void)paths;
  (void)npaths;
}
static void r_tris(void *u, NVGpaint *p, NVGcompositeOperationState op,
                   NVGscissor *s, const NVGvertex *v, int n, float fringe) {
  (void)u; (void)p; (void)op; (void)s; (void)v; (void)n; (void)fringe;
}
static void r_delete(void *u) { (void)u; rec.deleted = 1; }

int main(void) {
  NVGcolor c, d;
  float m[6], n[6], inv[6], x, y;
  NVGparams params;
  NVGcontext *ctx;
  int img, w, h;

  /* Colours. */
  c = nvgRGBA(255, 128, 0, 51);
  CHECK(NEAR(c.r, 1.0f) && NEAR(c.g, 128.0f / 255.0f) && NEAR(c.b, 0.0f));
  CHECK(NEAR(c.a, 0.2f) && NEAR(c.rgba[3], c.a));
  c = nvgRGBf(0.25f, 0.5f, 0.75f);
  CHECK(NEAR(c.r, 0.25f) && NEAR(c.g, 0.5f) && NEAR(c.b, 0.75f) && NEAR(c.a, 1.0f));
  d = nvgLerpRGBA(nvgRGBA(0, 0, 0, 0), nvgRGBA(255, 255, 255, 255), 0.5f);
  CHECK(NEAR(d.r, 0.5f) && NEAR(d.a, 0.5f));
  d = nvgTransRGBAf(c, 0.125f);
  CHECK(NEAR(d.b, 0.75f) && NEAR(d.a, 0.125f));
  d = nvgHSL(0.0f, 1.0f, 0.5f); /* pure red */
  CHECK(NEAR(d.r, 1.0f) && NEAR(d.g, 0.0f) && NEAR(d.b, 0.0f) && NEAR(d.a, 1.0f));
  d = nvgHSLA(1.0f / 3.0f, 1.0f, 0.5f, 255); /* pure green */
  CHECK(NEAR(d.r, 0.0f) && NEAR(d.g, 1.0f) && NEAR(d.b, 0.0f));

  /* Angles. */
  CHECK(NEAR(nvgDegToRad(180.0f), NVG_PI) && NEAR(nvgRadToDeg(NVG_PI / 2), 90.0f));

  /* Transforms: translate, scale, multiply, inverse. */
  nvgTransformIdentity(m);
  CHECK(m[0] == 1 && m[1] == 0 && m[2] == 0 && m[3] == 1 && m[4] == 0 && m[5] == 0);
  nvgTransformScale(m, 2.0f, 3.0f);
  nvgTransformTranslate(n, 10.0f, 20.0f);
  memcpy(inv, m, sizeof m);
  nvgTransformMultiply(m, n); /* scale, then translate */
  nvgTransformPoint(&x, &y, m, 1.0f, 1.0f);
  CHECK(NEAR(x, 12.0f) && NEAR(y, 23.0f));
  nvgTransformPremultiply(inv, n); /* translate, then scale */
  nvgTransformPoint(&x, &y, inv, 1.0f, 1.0f);
  CHECK(NEAR(x, 22.0f) && NEAR(y, 63.0f));
  CHECK(nvgTransformInverse(inv, m) == 1);
  nvgTransformPoint(&x, &y, inv, 12.0f, 23.0f);
  CHECK(NEAR(x, 1.0f) && NEAR(y, 1.0f));
  nvgTransformScale(n, 0.0f, 1.0f); /* singular */
  CHECK(nvgTransformInverse(inv, n) == 0);
  nvgTransformRotate(m, nvgDegToRad(90.0f));
  nvgTransformPoint(&x, &y, m, 1.0f, 0.0f);
  CHECK(NEAR(x, 0.0f) && NEAR(y, 1.0f));

  /* A context over the recording back-end: image decoding goes through the
     stb_image of the stb dependency, rendering through the callbacks. */
  memset(&params, 0, sizeof params);
  params.renderCreate = r_create;
  params.renderCreateTexture = r_create_tex;
  params.renderDeleteTexture = r_delete_tex;
  params.renderUpdateTexture = r_update_tex;
  params.renderGetTextureSize = r_tex_size;
  params.renderViewport = r_viewport;
  params.renderCancel = r_cancel;
  params.renderFlush = r_flush;
  params.renderFill = r_fill;
  params.renderStroke = r_stroke;
  params.renderTriangles = r_tris;
  params.renderDelete = r_delete;
  ctx = nvgCreateInternal(&params);
  CHECK(ctx != NULL);
  if (ctx == NULL) return 1;
  CHECK(rec.textures == 1); /* the font atlas */

  img = nvgCreateImageMem(ctx, 0, (unsigned char *)kPng, (int)sizeof kPng);
  CHECK(img == 2);
  CHECK(rec.last_w == 2 && rec.last_h == 2);
  {
    static const unsigned char want[16] = {255, 0, 0, 255, 0, 255, 0, 255,
                                           0, 0, 255, 255, 255, 255, 255, 128};
    CHECK(memcmp(rec.last_pixels, want, sizeof want) == 0);
  }
  CHECK(nvgCreateImageMem(ctx, 0, (unsigned char *)"not a png", 9) == 0);

  nvgBeginFrame(ctx, 100.0f, 100.0f, 1.0f);
  nvgBeginPath(ctx);
  nvgRect(ctx, 10.0f, 20.0f, 30.0f, 40.0f);
  nvgFillColor(ctx, nvgRGBA(255, 0, 0, 255));
  nvgFill(ctx);
  nvgEndFrame(ctx);
  CHECK(rec.fills == 1 && rec.fill_paths == 1 && rec.fill_convex == 1);
  CHECK(rec.fill_nfill == 4);
  CHECK(NEAR(rec.fill_bounds[0], 10.0f) && NEAR(rec.fill_bounds[1], 20.0f));
  CHECK(NEAR(rec.fill_bounds[2], 40.0f) && NEAR(rec.fill_bounds[3], 60.0f));
  CHECK(rec.flushes == 1);

  /* No font is loaded, and a non-font blob is refused. */
  CHECK(nvgFindFont(ctx, "sans") == -1);
  CHECK(nvgCreateFontMem(ctx, "bad", (unsigned char *)"not a font", 10, 0) == -1);

  nvgImageSize(ctx, img, &w, &h);
  CHECK(w == 2 && h == 2);
  nvgDeleteInternal(ctx);
  CHECK(rec.deleted == 1);

  if (failures == 0) printf("ok\n");
  return failures != 0;
}
