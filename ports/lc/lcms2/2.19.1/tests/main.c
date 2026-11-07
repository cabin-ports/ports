#include <lcms2.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fail(const char *what) {
  fprintf(stderr, "lcms2: %s\n", what);
  return 1;
}

static int logged_errors;

static void on_error(cmsContext ctx, cmsUInt32Number code, const char *text) {
  (void)ctx;
  (void)code;
  (void)text;
  ++logged_errors;
}

static int near(double got, double want, double tolerance) {
  return fabs(got - want) <= tolerance;
}

static const char kIt8[] =
    "CGATS.17\n"
    "NUMBER_OF_FIELDS 2\n"
    "BEGIN_DATA_FORMAT\n"
    "SAMPLE_ID RGB_R\n"
    "END_DATA_FORMAT\n"
    "NUMBER_OF_SETS 2\n"
    "BEGIN_DATA\n"
    "A1 0.5\n"
    "A2 1.25\n"
    "END_DATA\n";

int main(void) {
  const cmsUInt8Number rgb[9] = {255, 255, 255, 0, 0, 0, 255, 0, 0};
  cmsUInt8Number back[9];
  cmsCIELab lab[3];
  /* CIEDE2000 test pair 1 of Sharma, Wu and Dalal (2005): 2.0425. */
  const cmsCIELab pair1 = {50.0, 2.6772, -79.7751};
  const cmsCIELab pair2 = {50.0, 0.0, -82.7485};
  cmsHPROFILE srgb, lab4, reopened;
  cmsHTRANSFORM to_lab, to_rgb;
  cmsToneCurve *gamma, *inverse;
  cmsHANDLE it8;
  cmsUInt8Number id[16];
  cmsUInt32Number size = 0;
  struct tm created;
  cmsUInt8Number *mem;
  char text[256];
  int i, nonzero = 0;

  if (cmsGetEncodedCMMversion() != LCMS_VERSION || LCMS_VERSION != 2190)
    return fail("version");

  srgb = cmsCreate_sRGBProfile();
  lab4 = cmsCreateLab4Profile(NULL);
  if (srgb == NULL || lab4 == NULL) return fail("built-in profiles");
  to_lab = cmsCreateTransform(srgb, TYPE_RGB_8, lab4, TYPE_Lab_DBL,
                              INTENT_RELATIVE_COLORIMETRIC, 0);
  to_rgb = cmsCreateTransform(lab4, TYPE_Lab_DBL, srgb, TYPE_RGB_8,
                              INTENT_RELATIVE_COLORIMETRIC, 0);
  if (to_lab == NULL || to_rgb == NULL) return fail("transforms");
  cmsDoTransform(to_lab, rgb, lab, 3);
  if (!near(lab[0].L, 100.0, 0.5) || !near(lab[0].a, 0.0, 0.5) ||
      !near(lab[0].b, 0.0, 0.5))
    return fail("white");
  if (!near(lab[1].L, 0.0, 0.5)) return fail("black");
  /* sRGB red in the D50 ICC PCS. */
  if (!near(lab[2].L, 54.29, 1.0) || !near(lab[2].a, 80.81, 1.0) ||
      !near(lab[2].b, 69.89, 1.0))
    return fail("red");
  cmsDoTransform(to_rgb, lab, back, 3);
  for (i = 0; i < 9; ++i)
    if (abs((int)back[i] - (int)rgb[i]) > 1) return fail("round trip");

  /* ICC profiles are big-endian with "acsp" at byte 36. */
  if (!cmsSaveProfileToMem(srgb, NULL, &size) || size < 128)
    return fail("profile size");
  mem = (cmsUInt8Number *)malloc(size);
  if (mem == NULL || !cmsSaveProfileToMem(srgb, mem, &size))
    return fail("save profile");
  if (memcmp(mem + 36, "acsp", 4) != 0 ||
      ((cmsUInt32Number)mem[0] << 24 | (cmsUInt32Number)mem[1] << 16 |
       (cmsUInt32Number)mem[2] << 8 | mem[3]) != size)
    return fail("profile header");
  reopened = cmsOpenProfileFromMem(mem, size);
  if (reopened == NULL || cmsGetColorSpace(reopened) != cmsSigRgbData ||
      cmsGetDeviceClass(reopened) != cmsSigDisplayClass ||
      cmsGetPCS(reopened) != cmsSigXYZData)
    return fail("reopened profile");
  if (cmsGetProfileInfoASCII(reopened, cmsInfoDescription, "en", "US", text,
                             sizeof text) == 0 ||
      strstr(text, "sRGB") == NULL)
    return fail("description");
  if (!cmsMD5computeID(reopened)) return fail("MD5");
  cmsGetHeaderProfileID(reopened, id);
  for (i = 0; i < 16; ++i) nonzero |= id[i];
  if (!nonzero) return fail("profile ID");
  if (!cmsGetHeaderCreationDateTime(srgb, &created) ||
      created.tm_year + 1900 < 2025)
    return fail("creation time");

  gamma = cmsBuildGamma(NULL, 2.2);
  inverse = cmsReverseToneCurve(gamma);
  if (gamma == NULL || inverse == NULL) return fail("tone curves");
  if (!near(cmsEvalToneCurveFloat(gamma, 0.5f), pow(0.5, 2.2), 1e-4) ||
      !near(cmsEvalToneCurveFloat(inverse, (cmsFloat32Number)pow(0.5, 2.2)),
            0.5, 1e-3) ||
      !near(cmsEstimateGamma(gamma, 0.01), 2.2, 0.01))
    return fail("gamma");

  if (!near(cmsCIE2000DeltaE(&pair1, &pair2, 1, 1, 1), 2.0425, 1e-3))
    return fail("CIEDE2000");

  it8 = cmsIT8LoadFromMem(NULL, kIt8, (cmsUInt32Number)strlen(kIt8));
  if (it8 == NULL || !near(cmsIT8GetDataDbl(it8, "A2", "RGB_R"), 1.25, 1e-9))
    return fail("CGATS");

  cmsSetLogErrorHandler(on_error);
  if (cmsOpenProfileFromMem("not a profile", 13) != NULL || logged_errors == 0)
    return fail("error handler");

  cmsIT8Free(it8);
  cmsFreeToneCurve(inverse);
  cmsFreeToneCurve(gamma);
  cmsCloseProfile(reopened);
  free(mem);
  cmsDeleteTransform(to_rgb);
  cmsDeleteTransform(to_lab);
  cmsCloseProfile(lab4);
  cmsCloseProfile(srgb);
  return 0;
}
