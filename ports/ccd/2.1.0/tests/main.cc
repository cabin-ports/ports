// Collides convex hulls held in std::vector through libccd: a tetrahedron
// whose apex dips 0.2 into a cube, before and after a quaternion turn
// about the vertical axis, and the same pair moved apart.  C++98, the
// floor of libccd's headers.
#include <ccd/ccd.h>
#include <ccd/quat.h>

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <vector>

namespace {

bool ok = true;

void check(bool pass, const char* what) {
  if (!pass) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ok = false;
  }
}

bool near(ccd_real_t a, ccd_real_t b) { return std::fabs(a - b) <= CCD_REAL(0.001); }

ccd_vec3_t vec(ccd_real_t x, ccd_real_t y, ccd_real_t z) {
  ccd_vec3_t v;
  ccdVec3Set(&v, x, y, z);
  return v;
}

// The convex hull of its points; libccd only asks for support points.
struct Hull {
  std::vector<ccd_vec3_t> points;
  ccd_vec3_t center;

  void add(ccd_real_t x, ccd_real_t y, ccd_real_t z) { points.push_back(vec(x, y, z)); }

  void move(ccd_real_t x, ccd_real_t y, ccd_real_t z) {
    const ccd_vec3_t offset = vec(x, y, z);
    for (std::size_t i = 0; i < points.size(); ++i) ccdVec3Add(&points[i], &offset);
    ccdVec3Add(&center, &offset);
  }

  void rotate(const ccd_quat_t& q) {
    for (std::size_t i = 0; i < points.size(); ++i) ccdQuatRotVec(&points[i], &q);
    ccdQuatRotVec(&center, &q);
  }
};

}  // namespace

// libccd's callback types have C language linkage.
extern "C" {

static void hull_support(const void* obj, const ccd_vec3_t* dir, ccd_vec3_t* out) {
  const Hull& hull = *static_cast<const Hull*>(obj);
  std::size_t best = 0;
  for (std::size_t i = 1; i < hull.points.size(); ++i) {
    if (ccdVec3Dot(&hull.points[i], dir) > ccdVec3Dot(&hull.points[best], dir)) best = i;
  }
  ccdVec3Copy(out, &hull.points[best]);
}

static void hull_center(const void* obj, ccd_vec3_t* out) {
  ccdVec3Copy(out, &static_cast<const Hull*>(obj)->center);
}

}

namespace {

Hull cube() {
  Hull hull;
  for (int i = 0; i < 8; ++i) {
    hull.add(i & 1 ? 1 : -1, i & 2 ? 1 : -1, i & 4 ? 1 : -1);
  }
  hull.center = vec(0, 0, 0);
  return hull;
}

// Its base lies at z = 2; its apex reaches z = 0.8, 0.2 below the cube's top.
Hull tetrahedron() {
  Hull hull;
  hull.add(CCD_REAL(-1.5), -1, 2);
  hull.add(CCD_REAL(1.5), -1, 2);
  hull.add(0, CCD_REAL(1.5), 2);
  hull.add(0, 0, CCD_REAL(0.8));
  hull.center = vec(0, CCD_REAL(-0.125), CCD_REAL(1.7));
  return hull;
}

ccd_t setup() {
  ccd_t ccd;
  CCD_INIT(&ccd);
  ccd.support1 = hull_support;
  ccd.support2 = hull_support;
  ccd.center1 = hull_center;
  ccd.center2 = hull_center;
  return ccd;
}

void penetration(const Hull& a, const Hull& b, const char* what) {
  const ccd_t ccd = setup();
  ccd_real_t depth = 0;
  // Zeroed, so a failed call leaves nothing uninitialized to check.
  ccd_vec3_t dir = vec(0, 0, 0), pos = vec(0, 0, 0);

  check(ccdGJKIntersect(&a, &b, &ccd) != 0 && ccdMPRIntersect(&a, &b, &ccd) != 0, what);
  check(ccdGJKPenetration(&a, &b, &ccd, &depth, &dir, &pos) == 0 && near(depth, CCD_REAL(0.2)) &&
            near(ccdVec3Z(&dir), 1),
        "EPA lifts the tetrahedron 0.2");
  check(ccdVec3Z(&pos) > CCD_REAL(0.75) && ccdVec3Z(&pos) < CCD_REAL(1.05),
        "EPA's contact lies at the apex");
  pos = vec(0, 0, 0);
  check(ccdMPRPenetration(&a, &b, &ccd, &depth, &dir, &pos) == 0 && near(depth, CCD_REAL(0.2)) &&
            near(ccdVec3Z(&dir), 1),
        "MPR lifts the tetrahedron 0.2");
  check(ccdVec3Z(&pos) > CCD_REAL(0.75) && ccdVec3Z(&pos) < CCD_REAL(1.05),
        "MPR's contact lies at the apex");
}

}  // namespace

int main() {
  const Hull box = cube();
  Hull tip = tetrahedron();
  penetration(box, tip, "the apex dips into the cube");

  // A turn about z keeps every height, so the contact stays the same.
  ccd_vec3_t axis = vec(0, 0, 1);
  ccd_quat_t q;
  ccdQuatSetAngleAxis(&q, CCD_REAL(0.7), &axis);
  tip.rotate(q);
  // (-1.5, -1) turned by 0.7 rad counterclockwise.
  const ccd_real_t c = std::cos(CCD_REAL(0.7)), s = std::sin(CCD_REAL(0.7));
  check(near(ccdVec3X(&tip.points[0]), -CCD_REAL(1.5) * c + s) &&
            near(ccdVec3Y(&tip.points[0]), -CCD_REAL(1.5) * s - c) &&
            near(ccdVec3Z(&tip.points[0]), 2) && near(ccdVec3Z(&tip.points[3]), CCD_REAL(0.8)) &&
            near(ccdQuatLen(&q), 1),
        "ccdQuatRotVec turns about z and keeps heights");
  penetration(box, tip, "the turned apex still dips into the cube");

  tip.move(0, 0, CCD_REAL(0.3));
  const ccd_t ccd = setup();
  ccd_vec3_t sep = vec(0, 0, 0);
  check(ccdGJKIntersect(&box, &tip, &ccd) == 0 && ccdMPRIntersect(&box, &tip, &ccd) == 0 &&
            ccdGJKSeparate(&box, &tip, &ccd, &sep) == -1,
        "0.1 above the cube, nothing touches");

  std::printf("ccd (C++): %s\n", ok ? "ok" : "FAILED");
  return ok ? 0 : 1;
}
