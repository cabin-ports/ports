#include <geos_c.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures = 0;

static void check(int ok, const char *what) {
  if (!ok) {
    fprintf(stderr, "FAIL: %s\n", what);
    failures++;
  }
}

static int errors = 0;

static void on_message(const char *message, void *userdata) {
  (void)message;
  (void)userdata;
  errors++;
}

static double area_of(GEOSContextHandle_t ctx, const GEOSGeometry *g) {
  double area = -1.0;
  if (g == NULL || !GEOSArea_r(ctx, g, &area)) {
    return -1.0;
  }
  return area;
}

/* Near-collinear points from upstream's OrientationIndexFailureTest, which
   orient consistently only through GEOS's double-double fallback. */
static int orientation_consistent(GEOSContextHandle_t ctx, double ax, double ay,
                                  double bx, double by, double cx, double cy) {
  int abc = GEOSOrientationIndex_r(ctx, ax, ay, bx, by, cx, cy);
  int bca = GEOSOrientationIndex_r(ctx, bx, by, cx, cy, ax, ay);
  int cab = GEOSOrientationIndex_r(ctx, cx, cy, ax, ay, bx, by);
  int acb = GEOSOrientationIndex_r(ctx, ax, ay, cx, cy, bx, by);
  return abc != 2 && abc == bca && abc == cab && acb == -abc;
}

int main(void) {
  GEOSContextHandle_t ctx = GEOS_init_r();
  GEOSContext_setErrorMessageHandler_r(ctx, on_message, NULL);

  check(strcmp(GEOSversion(), GEOS_CAPI_VERSION) == 0, "GEOSversion");
  check(strcmp(GEOS_CAPI_VERSION, "3.15.0-CAPI-1.21.0") == 0,
        "GEOS_CAPI_VERSION");

  GEOSWKTReader *reader = GEOSWKTReader_create_r(ctx);
  GEOSGeometry *a = GEOSWKTReader_read_r(
      ctx, reader, "POLYGON ((0 0, 10 0, 10 10, 0 10, 0 0))");
  GEOSGeometry *b = GEOSWKTReader_read_r(
      ctx, reader, "POLYGON ((5 5, 15 5, 15 15, 5 15, 5 5))");
  check(a != NULL && b != NULL && errors == 0, "WKT read");
  check(GEOSWKTReader_read_r(ctx, reader, "POLYGON ((0 0, 1") == NULL &&
            errors == 1,
        "malformed WKT reports an error");

  GEOSGeometry *inter = GEOSIntersection_r(ctx, a, b);
  GEOSGeometry *uni = GEOSUnion_r(ctx, a, b);
  GEOSGeometry *diff = GEOSDifference_r(ctx, a, b);
  check(area_of(ctx, inter) == 25.0, "intersection area");
  check(area_of(ctx, uni) == 175.0, "union area");
  check(area_of(ctx, diff) == 75.0, "difference area");

  char *matrix = GEOSRelate_r(ctx, a, b);
  check(matrix != NULL && strcmp(matrix, "212101212") == 0, "relate matrix");
  check(GEOSIntersects_r(ctx, a, b) == 1 && GEOSContains_r(ctx, a, b) == 0,
        "predicates");

  GEOSWKTWriter *writer = GEOSWKTWriter_create_r(ctx);
  GEOSWKTWriter_setTrim_r(ctx, writer, 1);
  GEOSNormalize_r(ctx, inter);
  char *wkt = GEOSWKTWriter_write_r(ctx, writer, inter);
  check(wkt != NULL &&
            strcmp(wkt, "POLYGON ((5 5, 5 10, 10 10, 10 5, 5 5))") == 0,
        "normalized intersection WKT");

  /* Formatted by the bundled Ryu. */
  GEOSGeometry *pt = GEOSGeom_createPointFromXY_r(ctx, 0.1, 1.0 / 3.0);
  char *pt_wkt = GEOSWKTWriter_write_r(ctx, writer, pt);
  check(pt_wkt != NULL &&
            strcmp(pt_wkt, "POINT (0.1 0.3333333333333333)") == 0,
        "point WKT via ryu");

  GEOSGeometry *circle = GEOSBuffer_r(ctx, pt, 1.0, 8);
  double circle_area = area_of(ctx, circle);
  /* A 32-gon inscribed in the unit circle: 16 sin(pi / 16). */
  check(fabs(circle_area - 16.0 * sin(3.14159265358979323846 / 16.0)) < 1e-12,
        "buffer area");

  /* Parsed and written by the bundled nlohmann/json. */
  GEOSGeoJSONReader *json_reader = GEOSGeoJSONReader_create_r(ctx);
  GEOSGeometry *line = GEOSGeoJSONReader_readGeometry_r(
      ctx, json_reader,
      "{\"type\":\"LineString\",\"coordinates\":[[0,0],[3,4],[3,10]]}");
  double length = -1.0;
  check(line != NULL && GEOSLength_r(ctx, line, &length) && length == 11.0,
        "GeoJSON line length");
  GEOSGeoJSONWriter *json_writer = GEOSGeoJSONWriter_create_r(ctx);
  char *json = GEOSGeoJSONWriter_writeGeometry_r(ctx, json_writer, line, -1);
  check(json != NULL &&
            strcmp(json, "{\"type\":\"LineString\",\"coordinates\":"
                         "[[0.0,0.0],[3.0,4.0],[3.0,10.0]]}") == 0,
        "GeoJSON write");

  check(orientation_consistent(ctx, 1.4540766091864998, -7.989685402102996,
                               23.131039116367354, -7.004368924503866,
                               1.4540766091865, -7.989685402102996),
        "robust orientation 1");
  check(orientation_consistent(ctx, 219.3649559090992, 140.84159161824724,
                               168.9018919682399, -5.713787599646864,
                               186.80814046338352, 46.28973405831556),
        "robust orientation 2");
  check(orientation_consistent(ctx, -140.8859438214298, 140.88594382142983,
                               -57.309236848216706, 57.30923684821671,
                               -190.9188309203678, 190.91883092036784),
        "robust orientation 8");

  GEOSFree_r(ctx, json);
  GEOSGeoJSONWriter_destroy_r(ctx, json_writer);
  GEOSGeom_destroy_r(ctx, line);
  GEOSGeoJSONReader_destroy_r(ctx, json_reader);
  GEOSGeom_destroy_r(ctx, circle);
  GEOSFree_r(ctx, pt_wkt);
  GEOSGeom_destroy_r(ctx, pt);
  GEOSFree_r(ctx, wkt);
  GEOSWKTWriter_destroy_r(ctx, writer);
  GEOSFree_r(ctx, matrix);
  GEOSGeom_destroy_r(ctx, diff);
  GEOSGeom_destroy_r(ctx, uni);
  GEOSGeom_destroy_r(ctx, inter);
  GEOSGeom_destroy_r(ctx, b);
  GEOSGeom_destroy_r(ctx, a);
  GEOSWKTReader_destroy_r(ctx, reader);
  GEOS_finish_r(ctx);

  if (failures != 0) {
    return 1;
  }
  printf("GEOS %s: overlapping squares intersect in area 25\n", GEOSversion());
  return 0;
}
