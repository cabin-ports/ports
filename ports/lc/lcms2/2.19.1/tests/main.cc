// C++98: the probe compiles at the interface minimum.
#include <lcms2.h>

#include <cmath>
#include <cstdio>

int main() {
  cmsHPROFILE srgb = cmsCreate_sRGBProfile();
  cmsHPROFILE xyz = cmsCreateXYZProfile();
  cmsHTRANSFORM t = cmsCreateTransform(srgb, TYPE_RGB_16, xyz, TYPE_XYZ_DBL,
                                       INTENT_RELATIVE_COLORIMETRIC, 0);
  if (t == NULL) {
    std::fprintf(stderr, "lcms2: transform\n");
    return 1;
  }
  const cmsUInt16Number white[3] = {65535, 65535, 65535};
  cmsCIEXYZ out;
  cmsDoTransform(t, white, &out, 1);
  // Relative colorimetric maps media white to the D50 PCS white point.
  const cmsCIEXYZ *d50 = cmsD50_XYZ();
  const bool ok = std::fabs(out.X - d50->X) < 0.002 &&
                  std::fabs(out.Y - d50->Y) < 0.002 &&
                  std::fabs(out.Z - d50->Z) < 0.002;
  cmsDeleteTransform(t);
  cmsCloseProfile(xyz);
  cmsCloseProfile(srgb);
  if (!ok) std::fprintf(stderr, "lcms2: D50 white\n");
  return ok ? 0 : 1;
}
