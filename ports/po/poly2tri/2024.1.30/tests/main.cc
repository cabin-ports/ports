// Drives poly2tri's constrained Delaunay triangulation: a unit square gives
// 2 triangles, a Steiner point inside it 4, and a square with a square hole 8
// triangles covering exactly the area outside the hole.
#include <poly2tri/poly2tri.h>

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, int line, const char *what)
{
  if (!ok) {
    std::fprintf(stderr, "main.cc:%d: check failed: %s\n", line, what);
    ++failures;
  }
}

#define CHECK(expr) check((expr), __LINE__, #expr)

double total_area(const std::vector<p2t::Triangle *> &triangles)
{
  double total = 0;
  for (std::size_t i = 0; i < triangles.size(); ++i) {
    const p2t::Point &a = *triangles[i]->GetPoint(0);
    const p2t::Point &b = *triangles[i]->GetPoint(1);
    const p2t::Point &c = *triangles[i]->GetPoint(2);
    total += std::fabs((b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y)) / 2;
  }
  return total;
}

std::vector<p2t::Point *> polyline(std::vector<p2t::Point> &points)
{
  std::vector<p2t::Point *> result;
  for (std::size_t i = 0; i < points.size(); ++i)
    result.push_back(&points[i]);
  return result;
}

std::vector<p2t::Point> square(double x0, double y0, double x1, double y1)
{
  std::vector<p2t::Point> points;
  points.push_back(p2t::Point(x0, y0));
  points.push_back(p2t::Point(x1, y0));
  points.push_back(p2t::Point(x1, y1));
  points.push_back(p2t::Point(x0, y1));
  return points;
}

void unit_square()
{
  std::vector<p2t::Point> points = square(0, 0, 1, 1);
  p2t::CDT cdt(polyline(points));
  cdt.Triangulate();
  std::vector<p2t::Triangle *> triangles = cdt.GetTriangles();
  CHECK(triangles.size() == 2);
  CHECK(std::fabs(total_area(triangles) - 1) < 1e-12);
}

void steiner_point()
{
  std::vector<p2t::Point> points = square(0, 0, 10, 10);
  // Off both diagonals, so no three input points are collinear.
  p2t::Point inside(3, 6);
  p2t::CDT cdt(polyline(points));
  cdt.AddPoint(&inside);
  cdt.Triangulate();
  std::vector<p2t::Triangle *> triangles = cdt.GetTriangles();
  CHECK(triangles.size() == 4);
  CHECK(std::fabs(total_area(triangles) - 100) < 1e-9);
  // The corners lie on one circle that holds the point, so every Delaunay
  // triangle has the point as a vertex.
  for (std::size_t i = 0; i < triangles.size(); ++i)
    CHECK(triangles[i]->Contains(&inside));
}

void square_with_hole()
{
  std::vector<p2t::Point> outer = square(0, 0, 4, 4);
  std::vector<p2t::Point> hole = square(1, 1, 3, 3);
  p2t::CDT cdt(polyline(outer));
  cdt.AddHole(polyline(hole));
  cdt.Triangulate();
  std::vector<p2t::Triangle *> triangles = cdt.GetTriangles();
  CHECK(triangles.size() == 8);
  CHECK(std::fabs(total_area(triangles) - 12) < 1e-9);
}

void point_inequality()
{
  // Points that differ in one coordinate are unequal.
  CHECK(p2t::Point(1, 2) != p2t::Point(1, 3));
  CHECK(p2t::Point(1, 2) != p2t::Point(2, 2));
  CHECK(!(p2t::Point(1, 2) != p2t::Point(1, 2)));
}

}  // namespace

int main()
{
  unit_square();
  steiner_point();
  square_with_hole();
  point_inequality();
  std::printf("poly2tri C++: %s\n", failures == 0 ? "ok" : "FAILED");
  return failures == 0 ? 0 : 1;
}
