#include <bezier/bezier.h>

#include <cmath>
#include <cstdio>

namespace {

int failures = 0;

#define CHECK(cond)                                                            \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__,    \
                   #cond);                                                     \
      ++failures;                                                              \
    }                                                                          \
  } while (0)

bool near(double a, double b) { return std::fabs(a - b) < 1e-9; }

bool near(const bezier::Point &p, double x, double y) {
  return near(p.x, x) && near(p.y, y);
}

} // namespace

int main() {
  // The README "General Usage" curve.  Expected values are its Bernstein form
  // evaluated by hand: B(t) = (1-t)^3 P0 + 3(1-t)^2 t P1 + 3(1-t) t^2 P2 + t^3 P3.
  const bezier::Bezier<3> b({{120, 160}, {35, 200}, {220, 260}, {220, 40}});
  CHECK(b.order() == 3 && b.size() == 4);
  CHECK(near(b.valueAt(0.0), 120, 160));
  CHECK(near(b.valueAt(0.25), 99.765625, 189.0625));
  CHECK(near(b.valueAt(0.5), 138.125, 197.5));
  CHECK(near(b.valueAt(1.0), 220, 40));
  CHECK(near(b.valueAt(1.0, 0), 220));
  CHECK(near(b.valueAt(0.75, 1), 157.1875));

  // Derivative weights are 3 (P[i+1] - P[i]): (-255, 120), (555, 180), (0, -660).
  const bezier::Bezier<2> d = b.derivative();
  CHECK(near(d.valueAt(0.0), -255, 120));
  CHECK(near(d.valueAt(0.5), 213.75, -45));
  CHECK(near(d.valueAt(1.0), 0, -660));

  // de Casteljau at t = 0.5: left runs from P0 and right from P3, both to B(0.5).
  const bezier::Bezier<3>::Split<3> halves = b.split(0.5);
  CHECK(near(halves.left[1], 77.5, 180));
  CHECK(near(halves.left[2], 102.5, 205));
  CHECK(near(halves.left.valueAt(1.0), 138.125, 197.5));
  CHECK(near(halves.left.valueAt(0.5), 99.765625, 189.0625));
  CHECK(near(halves.right[1], 220, 150));
  CHECK(near(halves.right[2], 173.75, 190));
  CHECK(near(halves.right.valueAt(0.0), 220, 40));
  CHECK(near(halves.right.valueAt(1.0), 138.125, 197.5));
  CHECK(near(halves.right.valueAt(0.5), 192.421875, 157.1875));

  // Upstream tests/basic-tests.cpp:60, :192, :262 and :266: a degree-4
  // binomial, t outside [0, 1], and a split away from the midpoint.
  CHECK(bezier::Bezier<4>::binomialCoefficients[2] == 6);
  CHECK(near(b.valueAt(-0.35), 327.983125, 138.2125));
  const bezier::Bezier<3>::Split<3> at08 = b.split(0.8);
  CHECK(near(at08.left[2], 156.8, 236.8));
  CHECK(near(at08.right[2], 212.6, 116.8));

  // A default curve has every control point at the origin.
  bezier::Bezier<2> q;
  q[1].set(1, 2);
  CHECK(near(q[0], 0, 0));
  CHECK(near(q.valueAt(0.5), 0.5, 1));

  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("bezier ok");
  return 0;
}
