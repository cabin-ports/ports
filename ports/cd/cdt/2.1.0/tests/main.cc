#include <CDT.h>

#include <cmath>
#include <cstdio>
#include <vector>

static int failures = 0;

#define CHECK(cond)                                         \
  do {                                                      \
    if (!(cond)) {                                          \
      std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #cond); \
      ++failures;                                           \
    }                                                       \
  } while (0)

static bool hasEdge(const CDT::Triangulation<double>& t, CDT::VertInd a,
                    CDT::VertInd b) {
  for (const CDT::Triangle& tr : t.triangles) {
    for (int i = 0; i < 3; ++i) {
      const CDT::VertInd p = tr.vertices[i];
      const CDT::VertInd q = tr.vertices[(i + 1) % 3];
      if ((p == a && q == b) || (p == b && q == a)) return true;
    }
  }
  return false;
}

int main() {
  // A 4x4 square, a point below its diagonal, and a duplicate of that point.
  std::vector<CDT::V2d<double>> pts = {
      {0, 0}, {4, 0}, {4, 4}, {0, 4}, {3, 1}, {3, 1}};
  std::vector<CDT::Edge> edges = {CDT::Edge(0, 2)};

  const CDT::DuplicatesInfo info = CDT::RemoveDuplicatesAndRemapEdges(pts, edges);
  CHECK(info.duplicates.size() == 1);
  CHECK(info.duplicates[0] == 5);
  CHECK(pts.size() == 5);

  CDT::Triangulation<double> cdt;
  cdt.insertVertices(pts);
  cdt.insertEdges(edges);
  cdt.eraseSuperTriangle();

  // Five points with four on the hull: 2 * 5 - 2 - 4 = 4 triangles.
  CHECK(cdt.triangles.size() == 4);
  CHECK(cdt.vertices.size() == 5);
  CHECK(cdt.fixedEdges.size() == 1);
  CHECK(cdt.fixedEdges.count(CDT::Edge(0, 2)) == 1);
  CHECK(hasEdge(cdt, 0, 2));
  CHECK(!hasEdge(cdt, 1, 3));

  double area = 0;
  for (const CDT::Triangle& tr : cdt.triangles) {
    const CDT::V2d<double>& a = cdt.vertices[tr.vertices[0]];
    const CDT::V2d<double>& b = cdt.vertices[tr.vertices[1]];
    const CDT::V2d<double>& c = cdt.vertices[tr.vertices[2]];
    area += std::fabs((b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y)) / 2;
  }
  CHECK(std::fabs(area - 16.0) < 1e-12);

  if (failures) return 1;
  std::puts("cdt ok");
  return 0;
}
