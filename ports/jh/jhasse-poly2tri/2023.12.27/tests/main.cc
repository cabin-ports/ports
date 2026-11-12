// Drives poly2tri's constrained Delaunay triangulation.  A square with a
// square hole must give 8 triangles covering the 96 units of area outside
// the hole, with every polygon edge kept as a triangle edge.  A Steiner
// point inside a square must be a vertex of all 4 Delaunay triangles.  A
// polyline whose consecutive points coincide must be rejected.
#include <poly2tri/poly2tri.h>

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <stdexcept>
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

double area(p2t::Triangle *t)
{
  const p2t::Point &a = *t->GetPoint(0);
  const p2t::Point &b = *t->GetPoint(1);
  const p2t::Point &c = *t->GetPoint(2);
  return std::fabs((b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y)) / 2;
}

double total_area(const std::vector<p2t::Triangle *> &triangles)
{
  double total = 0;
  for (p2t::Triangle *t : triangles)
    total += area(t);
  return total;
}

std::vector<p2t::Point *> pointers(std::vector<p2t::Point> &points)
{
  std::vector<p2t::Point *> result;
  for (p2t::Point &p : points)
    result.push_back(&p);
  return result;
}

// Whether every edge of the closed polyline is an edge of some triangle.
bool keeps_edges(const std::vector<p2t::Triangle *> &triangles,
                 const std::vector<p2t::Point *> &polyline)
{
  for (std::size_t i = 0; i < polyline.size(); ++i) {
    p2t::Point *p = polyline[i];
    p2t::Point *q = polyline[(i + 1) % polyline.size()];
    bool found = false;
    for (p2t::Triangle *t : triangles)
      found = found || t->Contains(p, q);
    if (!found)
      return false;
  }
  return true;
}

void square_with_hole()
{
  std::vector<p2t::Point> outer = {{0, 0}, {10, 0}, {10, 10}, {0, 10}};
  std::vector<p2t::Point> hole = {{4, 4}, {4, 6}, {6, 6}, {6, 4}};
  std::vector<p2t::Point *> outer_line = pointers(outer);
  std::vector<p2t::Point *> hole_line = pointers(hole);

  p2t::CDT cdt(outer_line);
  cdt.AddHole(hole_line);
  cdt.Triangulate();
  std::vector<p2t::Triangle *> triangles = cdt.GetTriangles();

  CHECK(triangles.size() == 8);
  CHECK(std::fabs(total_area(triangles) - 96) < 1e-9);
  CHECK(keeps_edges(triangles, outer_line));
  CHECK(keeps_edges(triangles, hole_line));
}

void steiner_point()
{
  std::vector<p2t::Point> square = {{0, 0}, {10, 0}, {10, 10}, {0, 10}};
  std::vector<p2t::Point *> square_line = pointers(square);
  // Off both diagonals, so no three input points are collinear.
  p2t::Point inside(3, 6);

  p2t::CDT cdt(square_line);
  cdt.AddPoint(&inside);
  cdt.Triangulate();
  std::vector<p2t::Triangle *> triangles = cdt.GetTriangles();

  CHECK(triangles.size() == 4);
  CHECK(std::fabs(total_area(triangles) - 100) < 1e-9);
  // The corners lie on one circle, which holds the point, so a triangle of
  // three corners would not be Delaunay.
  for (p2t::Triangle *t : triangles)
    CHECK(t->Contains(&inside));
}

void repeated_point()
{
  // The first edge repeats its point, so the constructor throws before it
  // allocates an edge, which upstream would not free.
  std::vector<p2t::Point> points = {{0, 0}, {0, 0}, {10, 0}, {0, 10}};
  std::vector<p2t::Point *> polyline = pointers(points);
  bool rejected = false;
  try {
    p2t::CDT cdt(polyline);
  } catch (const std::runtime_error &) {
    rejected = true;
  }
  CHECK(rejected);
}

}  // namespace

int main()
{
  square_with_hole();
  steiner_point();
  repeated_point();
  std::printf("jhasse-poly2tri C++: %s\n", failures == 0 ? "ok" : "FAILED");
  return failures == 0 ? 0 : 1;
}
