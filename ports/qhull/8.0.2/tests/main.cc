// qhull's C++ interface.  RboxPoints generates a cube's corners through
// libqhullcpp's qh_fprintf_rbox and Qhull computes their hull; output
// goes to a std::ostream through libqhullcpp's qh_fprintf; an input
// error longjmps out of the C library and becomes a QhullError.  Were
// libqhull_r's own userprintf_r.c or userprintf_rbox_r.c linked in their
// place, the run would stop with qhull's "fp is 0" internal errors
// (QH6028, QH6231), as the C++ classes hand them no FILE.
#include <cmath>
#include <cstdio>
#include <sstream>
#include <string>

#include <libqhullcpp/Qhull.h>
#include <libqhullcpp/QhullError.h>
#include <libqhullcpp/QhullFacet.h>
#include <libqhullcpp/QhullFacetList.h>
#include <libqhullcpp/QhullHyperplane.h>
#include <libqhullcpp/QhullVertexSet.h>
#include <libqhullcpp/RboxPoints.h>

namespace {

int failures = 0;

void check(bool ok, int line, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "main.cc:%d: check failed: %s\n", line, what);
    ++failures;
  }
}

#define CHECK(cond) check((cond), __LINE__, #cond)

bool near(double a, double b) { return std::fabs(a - b) < 1e-12; }

void check_cube() {
  // rbox 'c': the corners of the unit cube centered on the origin.
  orgQhull::RboxPoints cube("c");
  CHECK(cube.dimension() == 3);
  CHECK(cube.count() == 8);

  // The streams outlive the Qhull that points at them.
  std::ostringstream out;
  orgQhull::Qhull hull(cube, "");
  CHECK(hull.facetCount() == 6);
  CHECK(hull.vertexCount() == 8);
  CHECK(near(hull.area(), 6.0));
  CHECK(near(hull.volume(), 1.0));
  orgQhull::QhullFacetList facets = hull.facetList();
  int faces = 0;
  for (orgQhull::QhullFacetList::iterator i = facets.begin();
       i != facets.end(); ++i) {
    orgQhull::QhullFacet facet = *i;
    CHECK(facet.vertices().count() == 4);
    // Each face lies 0.5 from the center along one axis.
    orgQhull::QhullHyperplane plane = facet.hyperplane();
    CHECK(near(plane.offset(), -0.5));
    int axes = 0;
    for (int k = 0; k < 3; ++k) {
      if (near(std::fabs(plane[k]), 1.0)) {
        ++axes;
      }
    }
    CHECK(axes == 1);
    ++faces;
  }
  CHECK(faces == 6);

  // 'n' prints the facet normals: the normal size (4), the facet count,
  // then one line per facet.
  hull.setOutputStream(&out);
  hull.outputQhull("n");
  const std::string normals = out.str();
  CHECK(normals.compare(0, 4, "4\n6\n") == 0);
  int lines = 0;
  for (std::string::size_type i = 0; i < normals.size(); ++i) {
    if (normals[i] == '\n') {
      ++lines;
    }
  }
  CHECK(lines == 8);
}

void check_input_error() {
  // Three points cannot span a 3-d simplex.
  const double points[] = {0, 0, 0, 1, 0, 0, 0, 1, 0};
  std::ostringstream errors;
  orgQhull::Qhull hull;
  hull.setErrorStream(&errors);
  bool threw = false;
  try {
    hull.runQhull("triangle", 3, 3, points, "");
  } catch (const orgQhull::QhullError& e) {
    threw = e.errorCode() == 6214;
  }
  CHECK(threw);
}

}  // namespace

int main() {
  check_cube();
  check_input_error();
  if (failures != 0) {
    return 1;
  }
  // Not qh_version: libqhull_r.h:53 declares it outside its extern "C"
  // block, so MSVC decorates the name in C++ and it does not link.
  std::printf("qhull C++ API OK\n");
  return 0;
}
