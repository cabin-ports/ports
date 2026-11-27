#include <librttopo.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SQUARE_HEX                                                         \
  "0103000000010000000500000000000000000000000000000000000000000000000000" \
  "244000000000000000000000000000002440000000000000244000000000000000000000" \
  "00000000244000000000000000000000000000000000"
#define SHIFTED_HEX                                                        \
  "0103000000010000000500000000000000000014400000000000001440000000000000" \
  "2E4000000000000014400000000000002E400000000000002E40000000000000144000" \
  "00000000002E4000000000000014400000000000001440"

static int failures = 0;
static int errors = 0;

static void check(int ok, const char *what) {
  if (!ok) {
    fprintf(stderr, "FAIL: %s\n", what);
    failures++;
  }
}

static void on_error(const char *fmt, va_list ap, void *arg) {
  (void)fmt;
  (void)ap;
  (void)arg;
  errors++;
}

static void on_notice(const char *fmt, va_list ap, void *arg) {
  (void)fmt;
  (void)ap;
  (void)arg;
}

static int wkt_is(const RTCTX *ctx, const RTGEOM *g, const char *expected) {
  size_t size = 0;
  char *wkt = rtgeom_to_wkt(ctx, g, RTWKT_ISO, 15, &size);
  int ok = wkt != NULL && strcmp(wkt, expected) == 0 && size == strlen(wkt) + 1;
  if (!ok) {
    fprintf(stderr, "got WKT: %s\n", wkt != NULL ? wkt : "(null)");
  }
  rtfree(ctx, wkt);
  return ok;
}

int main(void) {
  RTCTX *ctx = rtgeom_init(NULL, NULL, NULL);
  RTGEOM *square, *shifted, *line, *common, *merged, *bad;
  char *hex;
  uint8_t *wkb;
  size_t size = 0;

  rtgeom_set_error_logger(ctx, on_error, NULL);
  rtgeom_set_notice_logger(ctx, on_notice, NULL);

  square = rtgeom_from_hexwkb(ctx, SQUARE_HEX, RT_PARSER_CHECK_ALL);
  check(square != NULL, "parse square");
  check(wkt_is(ctx, square, "POLYGON((0 0,10 0,10 10,0 10,0 0))"), "square WKT");
  check(rtgeom_area(ctx, square) == 100.0, "square area");
  check(rtgeom_perimeter(ctx, square) == 40.0, "square perimeter");

  hex = rtgeom_to_hexwkb(ctx, square, RTWKB_NDR, &size);
  check(hex != NULL && strcmp(hex, SQUARE_HEX) == 0 && size == strlen(hex) + 1,
        "square hexwkb round trip");
  rtfree(ctx, hex);

  wkb = rtgeom_to_wkb(ctx, square, RTWKB_XDR, &size);
  check(wkb != NULL && size == 93 && wkb[0] == 0 && wkb[1] == 0 &&
            wkb[4] == 3 && wkb[8] == 1 && wkb[12] == 5,
        "square XDR WKB header");
  rtfree(ctx, wkb);

  line = rtgeom_from_hexwkb(
      ctx,
      "010200000002000000000000000000000000000000000000000000000000000840"
      "0000000000001040",
      RT_PARSER_CHECK_ALL);
  check(line != NULL && wkt_is(ctx, line, "LINESTRING(0 0,3 4)"), "line WKT");
  check(line != NULL && rtgeom_length(ctx, line) == 5.0, "line length");

  shifted = rtgeom_from_hexwkb(ctx, SHIFTED_HEX, RT_PARSER_CHECK_ALL);
  check(shifted != NULL, "parse shifted square");
  common = rtgeom_intersection(ctx, square, shifted);
  check(common != NULL && fabs(rtgeom_area(ctx, common) - 25.0) < 1e-9,
        "GEOS intersection area");
  merged = rtgeom_union(ctx, square, shifted);
  check(merged != NULL && fabs(rtgeom_area(ctx, merged) - 175.0) < 1e-9,
        "GEOS union area");
  check(rtgeom_is_simple(ctx, square) == 1, "GEOS is_simple");
  check(strncmp(rtgeom_geos_version(), "3.15.0", 6) == 0, "GEOS version");

  check(errors == 0, "no errors on valid input");
  bad = rtgeom_from_hexwkb(ctx, "0203000000", RT_PARSER_CHECK_ALL);
  check(bad == NULL && errors > 0, "bad endian flag is rejected");

  rtgeom_free(ctx, merged);
  rtgeom_free(ctx, common);
  rtgeom_free(ctx, shifted);
  rtgeom_free(ctx, line);
  rtgeom_free(ctx, square);
  rtgeom_finish(ctx);

  if (failures != 0) {
    return EXIT_FAILURE;
  }
  puts("librttopo OK");
  return EXIT_SUCCESS;
}
