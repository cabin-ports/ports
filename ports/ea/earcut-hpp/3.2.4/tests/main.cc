// earcut.hpp: a quad triangulates to earcut.js's own expected indices; a
// square with a square hole, given either winding and as int or double
// points, gives 8 counter-clockwise triangles covering exactly its area; a
// custom point type works through mapbox::util::nth; degenerate input gives
// no triangles; refine() flips a bad diagonal and keeps a good one; and a
// polygon of over 80 vertices with three holes, which takes the hashed
// path, is covered exactly.
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <mapbox/earcut.hpp>
#include <utility>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, int line, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "main.cc:%d: check failed: %s\n", line, what);
    ++failures;
  }
}

#define CHECK(expr) check(static_cast<bool>(expr), __LINE__, #expr)

struct IntPoint {
  std::int64_t X, Y;
};

}  // namespace

namespace mapbox {
namespace util {
template <>
struct nth<0, IntPoint> {
  static std::int64_t get(const IntPoint& p) { return p.X; }
};
template <>
struct nth<1, IntPoint> {
  static std::int64_t get(const IntPoint& p) { return p.Y; }
};
}  // namespace util
}  // namespace mapbox

namespace {

// Twice the signed area of triangle abc: positive when counter-clockwise
// with y pointing up.
template <typename Point>
double cross(const Point& a, const Point& b, const Point& c) {
  using mapbox::util::nth;
  const double ax = static_cast<double>(nth<0, Point>::get(a));
  const double ay = static_cast<double>(nth<1, Point>::get(a));
  const double bx = static_cast<double>(nth<0, Point>::get(b));
  const double by = static_cast<double>(nth<1, Point>::get(b));
  const double cx = static_cast<double>(nth<0, Point>::get(c));
  const double cy = static_cast<double>(nth<1, Point>::get(c));
  return (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
}

// Checks a triangulation of the given vertices (numbered ring after ring):
// the triangle count, every index in range and every vertex used, every
// triangle counter-clockwise and non-degenerate, and the total area.
template <typename Point, typename N>
bool covers(const std::vector<Point>& vertices, const std::vector<N>& indices,
            std::size_t triangles, double area) {
  if (indices.size() != triangles * 3) {
    return false;
  }
  std::vector<bool> used(vertices.size(), false);
  double doubled = 0;
  for (std::size_t i = 0; i < indices.size(); i += 3) {
    for (std::size_t k = 0; k < 3; ++k) {
      if (indices[i + k] >= vertices.size()) {
        return false;
      }
      used[indices[i + k]] = true;
    }
    const double c = cross(vertices[indices[i]], vertices[indices[i + 1]],
                           vertices[indices[i + 2]]);
    if (c <= 0) {
      return false;
    }
    doubled += c;
  }
  return std::all_of(used.begin(), used.end(), [](bool u) { return u; }) &&
         doubled == 2 * area;
}

template <typename Point>
std::vector<Point> flatten(const std::vector<std::vector<Point>>& polygon) {
  std::vector<Point> vertices;
  for (const auto& ring : polygon) {
    vertices.insert(vertices.end(), ring.begin(), ring.end());
  }
  return vertices;
}

// The README's polygon: a 100x100 square with a 50x50 hole, area 7500.
template <typename Point>
void check_square_with_hole(bool reversed) {
  std::vector<std::vector<Point>> polygon = {
      {Point{100, 0}, Point{100, 100}, Point{0, 100}, Point{0, 0}},
      {Point{75, 25}, Point{75, 75}, Point{25, 75}, Point{25, 25}},
  };
  if (reversed) {
    for (auto& ring : polygon) {
      std::reverse(ring.begin(), ring.end());
    }
  }
  const std::vector<std::uint32_t> indices = mapbox::earcut(polygon);
  CHECK(covers(flatten(polygon), indices, 8, 7500.0));
}

}  // namespace

int main() {
  {
    // earcut.js v3.2.4 test/test.js:10-11 expects exactly these indices.
    using Point = std::array<double, 2>;
    const std::vector<std::vector<Point>> quad = {
        {{{10, 0}}, {{0, 50}}, {{60, 60}}, {{70, 10}}}};
    const std::vector<std::uint32_t> expected = {1, 0, 3, 1, 3, 2};
    CHECK(mapbox::earcut(quad) == expected);
    const std::vector<std::uint16_t> narrow =
        mapbox::earcut<std::uint16_t>(quad);
    CHECK(std::vector<std::uint32_t>(narrow.begin(), narrow.end()) == expected);
  }

  check_square_with_hole<std::array<double, 2>>(false);
  check_square_with_hole<std::array<double, 2>>(true);
  check_square_with_hole<std::pair<int, int>>(false);
  check_square_with_hole<std::pair<int, int>>(true);

  {
    // A concave L of area 7 through the README's custom point type.
    const std::vector<std::vector<IntPoint>> l_shape = {
        {{0, 0}, {4, 0}, {4, 1}, {1, 1}, {1, 4}, {0, 4}}};
    const auto indices = mapbox::earcut(l_shape);
    CHECK(covers(l_shape[0], indices, 4, 7.0));

    // refine() keeps the triangle count and the covered area.
    auto refined = indices;
    mapbox::refine(refined, l_shape[0]);
    CHECK(covers(l_shape[0], refined, 4, 7.0));
  }

  {
    using Point = std::pair<double, double>;
    const std::vector<std::vector<Point>> empty;
    CHECK(mapbox::earcut(empty).empty());
    // Points on y = 2x enclose nothing.  Whole coordinates keep every
    // product exact, so the result does not depend on whether the compiler
    // fuses multiply-adds (see cabin.toml).
    const std::vector<std::vector<Point>> collinear = {
        {{0, 0}, {1, 2}, {2, 4}, {3, 6}}};
    CHECK(mapbox::earcut(collinear).empty());
  }

  {
    // earcut.js v3.2.4 test/test.js:162-182 (and earcut.hpp's
    // test/test.cpp:238-254): refine() flips the long diagonal of a skinny
    // quad and leaves a Delaunay diagonal alone.
    using Point = std::pair<double, double>;
    std::vector<std::uint32_t> bad = {2, 3, 0, 2, 0, 1};
    mapbox::refine(bad, std::vector<Point>{{0, 0}, {3, 0}, {10, 1}, {0, 2}});
    CHECK(bad == (std::vector<std::uint32_t>{2, 3, 1, 3, 0, 1}));

    std::vector<std::uint32_t> good = {2, 3, 0, 2, 0, 1};
    mapbox::refine(good, std::vector<Point>{{0, 0}, {5, 0}, {4, 1}, {0, 4}});
    CHECK(good == (std::vector<std::uint32_t>{2, 3, 0, 2, 0, 1}));
  }

  {
    // Over 80 vertices switches earcut to its z-order hashed ear search: a
    // 200x100 rectangle with a 50-tooth zigzag top (area 21000) and three
    // 20x20 holes.  No two hole edges share a line, so the triangulation
    // has V + 2H - 2 = 115 + 6 - 2 triangles.
    using Point = std::pair<int, int>;
    std::vector<std::vector<Point>> polygon(1);
    polygon[0] = {{0, 0}, {200, 0}};
    for (int i = 0; i <= 100; ++i) {
      polygon[0].push_back({200 - 2 * i, 100 + 10 * (i % 2)});
    }
    polygon.push_back({{20, 20}, {40, 20}, {40, 40}, {20, 40}});
    polygon.push_back({{60, 50}, {80, 50}, {80, 70}, {60, 70}});
    polygon.push_back({{150, 25}, {170, 25}, {170, 45}, {150, 45}});
    const std::vector<Point> vertices = flatten(polygon);
    const auto indices = mapbox::earcut(polygon);
    CHECK(covers(vertices, indices, 119, 19800.0));
    auto refined = indices;
    mapbox::refine(refined, vertices);
    CHECK(covers(vertices, refined, 119, 19800.0));
  }

  if (failures != 0) {
    return 1;
  }
  std::printf("earcut ok\n");
  return 0;
}
