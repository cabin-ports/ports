// Consumer: boolean operations under both fill rules, the polygon tree,
// open-path clipping, offsetting, the full 64-bit coordinate range, and
// the polygon utilities, each checked against exact results.
#include <polyclipping/clipper.hpp>

#include <clipper.hpp>  // upstream's pkg-config spelling, the same header

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>

using namespace ClipperLib;

namespace {

int failures = 0;

#define CHECK(cond)                                                   \
  do {                                                                \
    if (!(cond)) {                                                    \
      std::printf("main.cc:%d: check failed: %s\n", __LINE__, #cond); \
      ++failures;                                                     \
    }                                                                 \
  } while (0)

Path rect(cInt left, cInt top, cInt right, cInt bottom) {
  Path p;
  p << IntPoint(left, top) << IntPoint(right, top) << IntPoint(right, bottom)
    << IntPoint(left, bottom);
  return p;
}

double total_area(const Paths& paths) {
  double sum = 0;
  for (size_t i = 0; i < paths.size(); ++i) sum += Area(paths[i]);
  return sum;
}

// The path's vertices, sorted.
Path vertices(Path p) {
  std::sort(p.begin(), p.end(), [](const IntPoint& a, const IntPoint& b) {
    return a.X != b.X ? a.X < b.X : a.Y < b.Y;
  });
  return p;
}

// Whether a path has the shape's vertices: Clipper returns paths in no
// specific order.
bool has(const Paths& paths, const Path& shape) {
  for (size_t i = 0; i < paths.size(); ++i)
    if (vertices(paths[i]) == vertices(shape)) return true;
  return false;
}

Paths execute(ClipType op, const Paths& subject, const Paths& clip,
              PolyFillType fill) {
  Clipper c;
  c.AddPaths(subject, ptSubject, true);
  c.AddPaths(clip, ptClip, true);
  Paths out;
  CHECK(c.Execute(op, out, fill));
  return out;
}

void boolean_operations() {
  // Two 100x100 squares overlapping in a 50x50 corner.
  const Paths a(1, rect(0, 0, 100, 100));
  const Paths b(1, rect(50, 50, 150, 150));
  Paths out = execute(ctIntersection, a, b, pftNonZero);
  CHECK(out.size() == 1 && vertices(out[0]) == vertices(rect(50, 50, 100, 100)));
  out = execute(ctUnion, a, b, pftNonZero);
  CHECK(out.size() == 1 && out[0].size() == 8 && std::fabs(total_area(out)) == 17500);
  out = execute(ctDifference, a, b, pftNonZero);
  CHECK(out.size() == 1 && out[0].size() == 6 && std::fabs(total_area(out)) == 7500);
  out = execute(ctXor, a, b, pftNonZero);
  CHECK(out.size() == 2 && std::fabs(total_area(out)) == 15000);

  // Both squares as one subject: even-odd leaves their overlap unfilled,
  // non-zero fills it.
  Paths both = a;
  both.push_back(b[0]);
  CHECK(std::fabs(total_area(execute(ctUnion, both, Paths(), pftEvenOdd))) == 15000);
  CHECK(std::fabs(total_area(execute(ctUnion, both, Paths(), pftNonZero))) == 17500);
}

void polygon_tree() {
  // A square with a square hole, then an island inside the hole.
  Paths framed(1, rect(0, 0, 100, 100));
  framed.push_back(rect(25, 25, 75, 75));
  framed.push_back(rect(40, 40, 60, 60));
  Clipper c;
  c.AddPaths(framed, ptSubject, true);
  PolyTree tree;
  CHECK(c.Execute(ctUnion, tree, pftEvenOdd));
  CHECK(tree.Total() == 3 && tree.ChildCount() == 1);
  if (tree.ChildCount() != 1) return;
  const PolyNode* outer = tree.Childs[0];
  CHECK(!outer->IsHole() && !outer->IsOpen() && outer->ChildCount() == 1);
  CHECK(std::fabs(Area(outer->Contour)) == 10000);
  if (outer->ChildCount() != 1) return;
  const PolyNode* hole = outer->Childs[0];
  CHECK(hole->IsHole() && hole->Parent == outer && hole->ChildCount() == 1);
  CHECK(std::fabs(Area(hole->Contour)) == 2500);
  // Clipper orients holes against their outers.
  CHECK(Orientation(outer->Contour) != Orientation(hole->Contour));
  if (hole->ChildCount() != 1) return;
  CHECK(!hole->Childs[0]->IsHole() && std::fabs(Area(hole->Childs[0]->Contour)) == 400);
  CHECK(tree.GetFirst() == outer && outer->GetNext() == hole &&
        hole->GetNext() == hole->Childs[0] && hole->Childs[0]->GetNext() == nullptr);

  Paths flat;
  PolyTreeToPaths(tree, flat);
  CHECK(flat.size() == 3 && std::fabs(total_area(flat)) == 10000 - 2500 + 400);
}

void open_paths() {
  // A line through the square and a line beside it: only the inside part
  // of the first survives, as an open path.
  Clipper c;
  Path through, beside;
  through << IntPoint(-50, 50) << IntPoint(150, 50);
  beside << IntPoint(-50, 200) << IntPoint(150, 200);
  c.AddPath(through, ptSubject, false);
  c.AddPath(beside, ptSubject, false);
  c.AddPath(rect(0, 0, 100, 100), ptClip, true);

  // Open paths need a PolyTree.
  Paths flat;
  bool threw = false;
  try {
    c.Execute(ctIntersection, flat);
  } catch (const clipperException& e) {
    threw = std::strstr(e.what(), "PolyTree") != nullptr;
  }
  CHECK(threw);

  PolyTree tree;
  CHECK(c.Execute(ctIntersection, tree, pftNonZero));
  Paths open, closed;
  OpenPathsFromPolyTree(tree, open);
  ClosedPathsFromPolyTree(tree, closed);
  Path segment;
  segment << IntPoint(0, 50) << IntPoint(100, 50);
  CHECK(closed.empty() && open.size() == 1 && vertices(open[0]) == segment);
  CHECK(tree.ChildCount() == 1 && tree.Childs[0]->IsOpen());
}

void offsets() {
  const Path square = rect(0, 0, 100, 100);
  ClipperOffset off;
  Paths out;

  off.AddPath(square, jtMiter, etClosedPolygon);
  off.Execute(out, 10);
  CHECK(out.size() == 1 && out[0].size() == 4 && std::fabs(total_area(out)) == 120 * 120);
  off.Execute(out, -10);
  CHECK(out.size() == 1 && std::fabs(total_area(out)) == 80 * 80);
  off.Execute(out, -60);
  CHECK(out.empty());

  // At the default arc tolerance, round joins step each corner's quarter
  // circle in four chords, inside the exact 14000 + 100 * pi.
  off.Clear();
  off.AddPath(square, jtRound, etClosedPolygon);
  off.Execute(out, 10);
  CHECK(out.size() == 1 && out[0].size() == 16 && std::fabs(total_area(out)) == 14304);

  // An open line 100 long widened by 5 on each side; square ends add 5
  // at each end.
  Path line;
  line << IntPoint(0, 0) << IntPoint(100, 0);
  off.Clear();
  off.AddPath(line, jtMiter, etOpenButt);
  off.Execute(out, 5);
  CHECK(out.size() == 1 && std::fabs(total_area(out)) == 100 * 10);
  off.Clear();
  off.AddPath(line, jtMiter, etOpenSquare);
  off.Execute(out, 5);
  CHECK(out.size() == 1 && std::fabs(total_area(out)) == 110 * 10);

  // The tree form nests a frame's hole, which a negative offset widens.
  Paths frame(1, square);
  const Path inner = rect(30, 30, 70, 70);
  frame.push_back(Path(inner.rbegin(), inner.rend()));
  off.Clear();
  off.AddPaths(frame, jtMiter, etClosedPolygon);
  PolyTree tree;
  off.Execute(tree, -5);
  CHECK(tree.Total() == 2 && tree.ChildCount() == 1 &&
        tree.Childs[0]->ChildCount() == 1 && tree.Childs[0]->Childs[0]->IsHole());
  Paths ring;
  PolyTreeToPaths(tree, ring);
  CHECK(std::fabs(total_area(ring)) == 90 * 90 - 50 * 50);
}

void full_range() {
  // Coordinates past loRange switch Clipper to its 128-bit products.
  const cInt k = 1000000000000LL;  // 1e12
  const Paths a(1, rect(0, 0, 3 * k, 3 * k));
  const Paths b(1, rect(k, k, 4 * k, 4 * k));
  Paths out = execute(ctIntersection, a, b, pftNonZero);
  CHECK(out.size() == 1 && vertices(out[0]) == vertices(rect(k, k, 3 * k, 3 * k)));
  // Clipper drops a vertex on the line through its neighbours by
  // comparing cross products, past loRange in 128 bits.  At each corner of
  // this triangle they are 0 and 2^64, which 64 bits cannot tell apart.
  const cInt big = cInt(1) << 32;
  Path corner;
  corner << IntPoint(0, 0) << IntPoint(big, 0) << IntPoint(0, big);
  Paths cleaned;
  SimplifyPolygon(corner, cleaned);
  CHECK(cleaned.size() == 1 && vertices(cleaned[0]) == vertices(corner));
  // Products near 1e25: the vertex at (2k, 4k) is on the line from the
  // origin to (3k, 6k), with products 4k * k and 2k * 2k; the tent's apex
  // is not, with products k * k and -k * k; the sliver's apex sits one unit
  // off its line.
  Path kite;
  kite << IntPoint(0, 0) << IntPoint(2 * k, 4 * k) << IntPoint(3 * k, 6 * k)
       << IntPoint(0, 6 * k);
  Path triangle;
  triangle << IntPoint(0, 0) << IntPoint(3 * k, 6 * k) << IntPoint(0, 6 * k);
  SimplifyPolygon(kite, cleaned);
  CHECK(cleaned.size() == 1 && vertices(cleaned[0]) == vertices(triangle));
  Path tent;
  tent << IntPoint(0, 0) << IntPoint(k, k) << IntPoint(2 * k, 0);
  SimplifyPolygon(tent, cleaned);
  CHECK(cleaned.size() == 1 && vertices(cleaned[0]) == vertices(tent));
  Path sliver;
  sliver << IntPoint(0, 0) << IntPoint(3 * k, 3 * k + 1) << IntPoint(6 * k, 6 * k);
  SimplifyPolygon(sliver, cleaned);
  CHECK(cleaned.size() == 1 && vertices(cleaned[0]) == vertices(sliver));

  bool threw = false;
  try {
    Clipper c;
    c.AddPath(rect(0, 0, hiRange + 1, 10), ptSubject, true);
  } catch (const clipperException& e) {
    threw = std::strcmp(e.what(), "Coordinate outside allowed range") == 0;
  }
  CHECK(threw);
}

void utilities() {
  // rect() runs counter-clockwise with Y up: positive area, the
  // orientation Clipper gives outer polygons.
  Path square = rect(0, 0, 100, 100);
  CHECK(Area(square) == 10000 && Orientation(square));
  ReversePath(square);
  CHECK(Area(square) == -10000 && !Orientation(square));

  CHECK(PointInPolygon(IntPoint(50, 50), square) == 1);
  CHECK(PointInPolygon(IntPoint(150, 50), square) == 0);
  CHECK(PointInPolygon(IntPoint(100, 30), square) == -1);
  CHECK(PointInPolygon(IntPoint(0, 0), square) == -1);

  // A bow tie splits into its two triangles.
  Path bowtie;
  bowtie << IntPoint(0, 0) << IntPoint(100, 100) << IntPoint(100, 0) << IntPoint(0, 100);
  Paths parts;
  SimplifyPolygon(bowtie, parts);
  CHECK(parts.size() == 2 && parts[0].size() == 3 && parts[1].size() == 3);
  CHECK(parts.size() == 2 && std::fabs(Area(parts[0])) == 2500 &&
        std::fabs(Area(parts[1])) == 2500);

  // A midpoint, a vertex one unit off an edge and a duplicate all go.
  Path noisy;
  noisy << IntPoint(0, 0) << IntPoint(50, 0) << IntPoint(100, 0) << IntPoint(100, 0)
        << IntPoint(101, 50) << IntPoint(100, 100) << IntPoint(0, 100);
  Path clean;
  CleanPolygon(noisy, clean);
  CHECK(clean.size() == 4 && std::fabs(Area(clean)) == 10000);

  // Minkowski sweeps a 2x2 square around a 10x10 square's outline: a
  // ring from 0 to 12 around a hole from 2 to 10.  The difference sweeps
  // the second path minus the first.
  const Path pattern = rect(0, 0, 2, 2);
  Paths sum;
  MinkowskiSum(pattern, rect(0, 0, 10, 10), sum, true);
  CHECK(sum.size() == 2 && std::fabs(total_area(sum)) == 12 * 12 - 8 * 8);
  CHECK(has(sum, rect(0, 0, 12, 12)) && has(sum, rect(2, 2, 10, 10)));
  Paths diff;
  MinkowskiDiff(rect(0, 0, 10, 10), pattern, diff);
  CHECK(diff.size() == 2 && std::fabs(total_area(diff)) == 12 * 12 - 8 * 8);
  CHECK(has(diff, rect(-10, -10, 2, 2)) && has(diff, rect(-8, -8, 0, 0)));

  std::ostringstream text;
  Path pair;
  pair << IntPoint(1, 2) << IntPoint(-3, 4);
  text << pair;
  CHECK(text.str() == "(1,2), (-3,4)\n");
}

}  // namespace

int main() {
  CHECK(std::strcmp(CLIPPER_VERSION, "6.4.2") == 0);
  boolean_operations();
  polygon_tree();
  open_paths();
  offsets();
  full_range();
  utilities();
  std::printf("polyclipping: %s\n", failures == 0 ? "ok" : "FAILED");
  return failures == 0 ? 0 : 1;
}
