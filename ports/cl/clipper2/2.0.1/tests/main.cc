#include <clipper2/clipper.h>

#include <cmath>
#include <cstdio>
#include <cstring>

// The interface definition the package exports, as upstream's CMake does.
#if !defined(CLIPPER2_MAX_DECIMAL_PRECISION) || \
    CLIPPER2_MAX_DECIMAL_PRECISION != 8
#error "CLIPPER2_MAX_DECIMAL_PRECISION must reach consumers as 8"
#endif

namespace {

int failures = 0;

void check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

bool near(double a, double b) { return std::fabs(a - b) < 1e-6; }

}  // namespace

int main() {
  using namespace Clipper2Lib;

  check(std::strcmp(CLIPPER2_VERSION, "2.0.1") == 0, "CLIPPER2_VERSION");

  // Two 100x100 squares overlapping in a 50x50 corner.
  const Paths64 a{MakePath({0, 0, 100, 0, 100, 100, 0, 100})};
  const Paths64 b{MakePath({50, 50, 150, 50, 150, 150, 50, 150})};
  check(std::fabs(Area(Intersect(a, b, FillRule::NonZero))) == 2500,
        "intersection area");
  check(std::fabs(Area(Union(a, b, FillRule::NonZero))) == 17500,
        "union area");
  check(std::fabs(Area(Difference(a, b, FillRule::NonZero))) == 7500,
        "difference area");
  check(std::fabs(Area(Xor(a, b, FillRule::NonZero))) == 15000, "xor area");

  // Offsetting a square outwards by 10 with mitred joins gives 120x120.
  check(std::fabs(Area(InflatePaths(a, 10, JoinType::Miter,
                                    EndType::Polygon))) == 14400,
        "inflated area");

  check(std::fabs(Area(RectClip(Rect64(25, 25, 75, 75), a))) == 2500,
        "rectangle clip area");

  check(PointInPolygon(Point64(50, 50), a[0]) ==
            PointInPolygonResult::IsInside,
        "point inside");
  check(PointInPolygon(Point64(0, 50), a[0]) == PointInPolygonResult::IsOn,
        "point on the edge");
  check(PointInPolygon(Point64(200, 200), a[0]) ==
            PointInPolygonResult::IsOutside,
        "point outside");

  // A square with a square hole: the tree nests the hole under its outer.
  Paths64 framed = a;
  framed.push_back(MakePath({25, 25, 25, 75, 75, 75, 75, 25}));
  PolyTree64 tree;
  BooleanOp(ClipType::Union, FillRule::EvenOdd, framed, Paths64(), tree);
  check(tree.Count() == 1 && tree.Child(0)->Count() == 1 &&
            std::fabs(Area(tree.Child(0)->Child(0)->Polygon())) == 2500,
        "polytree nesting");
  check(std::fabs(Area(PolyTreeToPaths64(tree))) == 7500, "framed area");

  // An open line through the square keeps only its inside part, both
  // through the clipping engine and through the rectangle clipper.
  const Path64 line = MakePath({-50, 50, 150, 50});
  Clipper64 clipper;
  clipper.AddOpenSubject(Paths64{line});
  clipper.AddClip(a);
  Paths64 closed, open;
  check(clipper.Execute(ClipType::Intersection, FillRule::NonZero, closed,
                        open) &&
            closed.empty() && open.size() == 1 && Length(open[0]) == 100,
        "open path intersection");
  const Paths64 lines = RectClipLines(Rect64(0, 0, 100, 100), line);
  check(lines.size() == 1 && Length(lines[0]) == 100, "rectangle line clip");

  // The triangulation module splits the square into two triangles.
  Paths64 triangles;
  check(Triangulate(a, triangles) == TriangulateResult::success &&
            triangles.size() == 2 && triangles[0].size() == 3 &&
            std::fabs(Area(triangles)) == 10000,
        "triangulation");

  // Floating-point paths scale by 10^precision; 8 is the most allowed.
  const PathsD c{MakePathD({0, 0, 1, 0, 1, 1, 0, 1})};
  const PathsD d{MakePathD({0.5, 0.5, 1.5, 0.5, 1.5, 1.5, 0.5, 1.5})};
  check(near(std::fabs(Area(Intersect(c, d, FillRule::NonZero, 8))), 0.25),
        "PathsD intersection at precision 8");
  bool rejected = false;
  try {
    Intersect(c, d, FillRule::NonZero, 9);
  } catch (const Clipper2Exception& e) {
    rejected = std::strstr(e.what(), "Precision") != nullptr;
  }
  check(rejected, "precision 9 exceeds CLIPPER2_MAX_DECIMAL_PRECISION");

  if (failures != 0) {
    return 1;
  }
  std::printf("Clipper2 %s: union of two squares has area %.0f\n",
              CLIPPER2_VERSION,
              std::fabs(Area(Union(a, b, FillRule::NonZero))));
  return 0;
}
