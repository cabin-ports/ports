#include <Mathematics/BSRational.h>
#include <Mathematics/DistPointSegment.h>
#include <Mathematics/IntrLine3Plane3.h>
#include <Mathematics/Matrix3x3.h>
#include <Mathematics/Quaternion.h>
#include <Mathematics/UIntegerAP32.h>
#include <Mathematics/Vector3.h>

#include <cmath>
#include <cstdio>

static int failures = 0;

#define CHECK(cond)                                                    \
  do {                                                                 \
    if (!(cond)) {                                                     \
      std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__,      \
                   __LINE__, #cond);                                   \
      ++failures;                                                      \
    }                                                                  \
  } while (0)

int main() {
  using V3 = gte::Vector3<double>;

  V3 u{1.0, 2.0, 3.0}, v{4.0, 5.0, 6.0};
  CHECK(gte::Dot(u, v) == 32.0);
  V3 c = gte::Cross(u, v);
  CHECK(c[0] == -3.0 && c[1] == 6.0 && c[2] == -3.0);

  gte::Matrix3x3<double> m{1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 10.0};
  V3 mv = m * u;
  CHECK(mv[0] == 14.0 && mv[1] == 32.0 && mv[2] == 53.0);

  // 90 degrees about z: (x, y, z, w) = (0, 0, sin 45, cos 45).
  double const h = std::sqrt(0.5);
  gte::Quaternion<double> q(0.0, 0.0, h, h);
  V3 r = gte::Rotate(q, V3{1.0, 0.0, 0.0});
  CHECK(std::fabs(r[0]) < 1e-12 && std::fabs(r[1] - 1.0) < 1e-12 &&
        std::fabs(r[2]) < 1e-12);

  // Point (3, 4, 0) against the segment (0,0,0)-(10,0,0): closest (3,0,0).
  gte::DCPPoint3Segment3<double> dps;
  auto d = dps(V3{3.0, 4.0, 0.0},
               gte::Segment3<double>(V3{0.0, 0.0, 0.0}, V3{10.0, 0.0, 0.0}));
  CHECK(d.distance == 4.0 && d.parameter == 0.3);
  CHECK(d.closest[1][0] == 3.0 && d.closest[1][1] == 0.0);

  // Line through (1,2,-3) along +z meets the plane z = 5 at (1,2,5).
  gte::FIQuery<double, gte::Line3<double>, gte::Plane3<double>> fi;
  auto hit = fi(gte::Line3<double>(V3{1.0, 2.0, -3.0}, V3{0.0, 0.0, 1.0}),
                gte::Plane3<double>(V3{0.0, 0.0, 1.0}, V3{0.0, 0.0, 5.0}));
  CHECK(hit.intersect && hit.numIntersections == 1);
  CHECK(hit.point[0] == 1.0 && hit.point[1] == 2.0 && hit.point[2] == 5.0);

  // Exact rational arithmetic.
  using Q = gte::BSRational<gte::UIntegerAP32>;
  Q third = Q(1) / Q(3), sixth = Q(1) / Q(6), half = Q(1) / Q(2);
  CHECK(third + sixth == half);
  CHECK(!(third + third == half));
  CHECK(static_cast<double>(half) == 0.5);

  if (failures == 0) std::puts("ok");
  return failures == 0 ? 0 : 1;
}
