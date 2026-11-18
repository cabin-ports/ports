#include <homog2d.hpp>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>

#define CHECK(cond)                                                \
  do {                                                             \
    if (!(cond)) {                                                 \
      std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #cond); \
      std::exit(1);                                                \
    }                                                              \
  } while (0)

static bool near(double a, double b) { return std::fabs(a - b) < 1e-9; }

int main() {
  using namespace h2d;

  // README.md's preview: two lines meet at (5,5); a translation prints [7,8].
  Line2d l1(Point2d(10, 10));
  Line2d l2(Point2d(0, 10), Point2d(10, 0));
  auto pt = l1 * l2;
  CHECK(near(pt.getX(), 5) && near(pt.getY(), 5));
  Homogr H(2, 3);
  std::ostringstream os;
  os << H * pt;
  CHECK(os.str() == "[7,8]");

  // x - 2y + 2 = 0 meets x + y - 10 = 0 at (6,4), whichever operand comes
  // first.
  Line2d la(Point2d(0, 1), Point2d(4, 3));
  auto p = la * l2;
  auto r = l2 * la;
  CHECK(near(p.getX(), 6) && near(p.getY(), 4));
  CHECK(near(r.getX(), 6) && near(r.getY(), 4));
  auto q = H * p;
  CHECK(near(q.getX(), 8) && near(q.getY(), 7));

  // Lines are stored normalized (a^2 + b^2 = 1).
  auto v = la.get();
  CHECK(near(v[0] * v[0] + v[1] * v[1], 1));
  CHECK(near(v[1], -2 * v[0]) && near(v[2], 2 * v[0]));

  CHECK(near(Segment(Point2d(1, 2), Point2d(4, 6)).length(), 5));
  CHECK(near(la.distTo(Point2d(1, 4)), std::sqrt(5.0)));
  CHECK(near(l1.distTo(Point2d(0, 10)), std::sqrt(50.0)));

  CHECK(std::string(HOMOG2D_VERSION) == "2.13.1");
  std::puts("homog2d ok");
}
