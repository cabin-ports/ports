/* Expected values come from h3's README.md (the Statue of Liberty example,
   lines 158-212) and its src/apps/testapps: testGridDisk.c:80-90 (res 0
   disk), testGridRing.c:26-45 (res 9 ring) and testBaseCells.c:23-28. */
#include <h3/h3api.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int failures = 0;

static void check(int ok, const char *what) {
  if (!ok) {
    fprintf(stderr, "FAIL: %s\n", what);
    failures++;
  }
}

static int in_set(H3Index h, const H3Index *set, int n) {
  int found = 0;
  for (int i = 0; i < n; i++) {
    found += set[i] == h;
  }
  return found;
}

int main(void) {
  check(H3_VERSION_MAJOR == 4 && H3_VERSION_MINOR == 6 && H3_VERSION_PATCH == 0,
        "version macros");

  LatLng liberty = {degsToRads(40.689167), degsToRads(-74.044444)};
  H3Index cell = 0;
  check(latLngToCell(&liberty, 10, &cell) == E_SUCCESS, "latLngToCell");
  check(cell == 0x8a2a1072b59ffffULL, "cell index");
  check(getResolution(cell) == 10 && isValidCell(cell) && !isPentagon(cell),
        "cell properties");

  char str[17];
  H3Index parsed = 0;
  check(h3ToString(cell, str, sizeof str) == E_SUCCESS &&
            strcmp(str, "8a2a1072b59ffff") == 0,
        "h3ToString");
  check(stringToH3("8a2a1072b59ffff", &parsed) == E_SUCCESS && parsed == cell,
        "stringToH3");

  LatLng center;
  check(cellToLatLng(cell, &center) == E_SUCCESS, "cellToLatLng");
  check(fabs(radsToDegs(center.lat) - 40.689422) < 1e-6 &&
            fabs(radsToDegs(center.lng) - -74.044431) < 1e-6,
        "cell center");

  static const double lat[6] = {40.690059, 40.689908, 40.689271,
                                40.688785, 40.688936, 40.689573};
  static const double lng[6] = {-74.044152, -74.045062, -74.045341,
                                -74.044711, -74.043801, -74.043521};
  CellBoundary boundary;
  check(cellToBoundary(cell, &boundary) == E_SUCCESS && boundary.numVerts == 6,
        "cellToBoundary");
  for (int i = 0; i < boundary.numVerts && i < 6; i++) {
    check(fabs(radsToDegs(boundary.verts[i].lat) - lat[i]) < 1e-6 &&
              fabs(radsToDegs(boundary.verts[i].lng) - lng[i]) < 1e-6,
          "boundary vertex");
  }

  /* The res 0 disk around San Francisco, and the res 9 ring and disk. */
  LatLng sf = {0.659966917655, 2 * 3.14159 - 2.1364398519396};
  H3Index sf0 = 0;
  H3Index disk0[7];
  static const H3Index expected0[7] = {
      0x8029fffffffffffULL, 0x801dfffffffffffULL, 0x8013fffffffffffULL,
      0x8027fffffffffffULL, 0x8049fffffffffffULL, 0x8051fffffffffffULL,
      0x8037fffffffffffULL};
  int64_t size = 0;
  check(latLngToCell(&sf, 0, &sf0) == E_SUCCESS && in_set(sf0, expected0, 7),
        "res 0 cell");
  check(maxGridDiskSize(1, &size) == E_SUCCESS && size == 7, "maxGridDiskSize");
  check(gridDisk(sf0, 1, disk0) == E_SUCCESS, "gridDisk res 0");
  for (int i = 0; i < 7; i++) {
    check(in_set(disk0[i], expected0, 7) == 1, "res 0 disk member");
  }

  H3Index sf9 = 0;
  H3Index disk9[7];
  static const H3Index ring9[6] = {0x89283080ddbffffULL, 0x89283080c37ffffULL,
                                   0x89283080c27ffffULL, 0x89283080d53ffffULL,
                                   0x89283080dcfffffULL, 0x89283080dc3ffffULL};
  check(latLngToCell(&sf, 9, &sf9) == E_SUCCESS, "res 9 cell");
  check(gridDisk(sf9, 1, disk9) == E_SUCCESS, "gridDisk res 9");
  for (int i = 0; i < 7; i++) {
    check(disk9[i] == sf9 ? 1 : in_set(disk9[i], ring9, 6) == 1,
          "res 9 disk member");
  }

  H3Index res0[122];
  check(res0CellCount() == 122 && pentagonCount() == 12, "cell counts");
  check(getRes0Cells(res0) == E_SUCCESS && res0[0] == 0x8001fffffffffffULL &&
            res0[121] == 0x80f3fffffffffffULL,
        "res 0 cells");

  if (failures != 0) {
    return 1;
  }
  puts("h3 ok");
  return 0;
}
