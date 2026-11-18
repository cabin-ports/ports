#include <delaunator.hpp>

#include <cstddef>
#include <cstdio>
#include <vector>

static int failures = 0;

static void check(bool ok, const char* what) {
  if (!ok) {
    std::printf("FAIL %s\n", what);
    ++failures;
  }
}

static bool equals(const std::vector<std::size_t>& got,
                   const std::vector<std::size_t>& want) {
  return got == want;
}

int main() {
  const std::size_t none = delaunator::INVALID_INDEX;

  // README's square; the expected values were measured.  Both area
  // functions return twice the geometric area.
  const std::vector<double> square = {-1, 1, 1, 1, 1, -1, -1, -1};
  delaunator::Delaunator s(square);
  check(equals(s.triangles, {0, 1, 2, 2, 3, 0}), "square triangles");
  check(equals(s.halfedges, {none, none, 5, none, none, 2}),
        "square halfedges");
  check(s.get_hull_area() == 8.0, "square hull area");
  check(s.get_triangle_area() == 8.0, "square triangle area");

  const std::vector<double> triangle = {0, 0, 4, 0, 0, 3};
  delaunator::Delaunator t(triangle);
  check(equals(t.triangles, {0, 2, 1}), "triangle indices");
  check(equals(t.halfedges, {none, none, none}), "triangle halfedges");
  check(t.get_hull_area() == 12.0, "triangle hull area");

  // Upstream's mapbox_issue_11 case, which needs an edge flip (measured).
  const std::vector<double> p = {516, 661, 369, 793, 426, 539, 273, 525,
                                 204, 694, 747, 750, 454, 390};
  delaunator::Delaunator d(p);
  check(d.triangles.size() == 3 * 7, "issue 11 triangle count");
  check(d.get_triangle_area() == d.get_hull_area(), "issue 11 areas");
  for (std::size_t e = 0; e < d.halfedges.size(); ++e) {
    const std::size_t o = d.halfedges[e];
    check(o == none || d.halfedges[o] == e, "issue 11 halfedge symmetry");
  }
  // Delaunay: no input point lies inside a triangle's circumcircle.
  for (std::size_t i = 0; i < d.triangles.size(); i += 3) {
    const double ax = p[2 * d.triangles[i]], ay = p[2 * d.triangles[i] + 1];
    const double bx = p[2 * d.triangles[i + 1]];
    const double by = p[2 * d.triangles[i + 1] + 1];
    const double cx = p[2 * d.triangles[i + 2]];
    const double cy = p[2 * d.triangles[i + 2] + 1];
    const double orient = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
    for (std::size_t k = 0; k < p.size(); k += 2) {
      const double dx = ax - p[k], dy = ay - p[k + 1];
      const double ex = bx - p[k], ey = by - p[k + 1];
      const double fx = cx - p[k], fy = cy - p[k + 1];
      const double incircle = (dx * dx + dy * dy) * (ex * fy - ey * fx) -
                              (ex * ex + ey * ey) * (dx * fy - dy * fx) +
                              (fx * fx + fy * fy) * (dx * ey - dy * ex);
      check(incircle * orient <= 0, "issue 11 empty circumcircles");
    }
  }

  if (failures != 0) {
    return 1;
  }
  std::printf("delaunator-cpp 1.0.0 C++ ok\n");
  return 0;
}
