// Labels polygons whose pole of inaccessibility is known: a square, frames
// whose holes swallow their centroids, and upstream's two degenerate cases.
// Distances are measured independently of the library.
#include <mapbox/polylabel.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <limits>

namespace {

namespace geo = mapbox::geometry;
using point = geo::point<double>;
using polygon = geo::polygon<double>;

bool ok = true;

void check(bool pass, const char* what) {
  if (!pass) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ok = false;
  }
}

// Distance from p to the nearest edge, negative outside (even-odd rule).
double signed_distance(const point& p, const polygon& poly) {
  bool inside = false;
  double nearest = std::numeric_limits<double>::infinity();
  for (const auto& ring : poly) {
    for (std::size_t i = 0, j = ring.size() - 1; i < ring.size(); j = i++) {
      const point& a = ring[i];
      const point& b = ring[j];
      if ((a.y > p.y) != (b.y > p.y) && p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x)
        inside = !inside;
      const double dx = b.x - a.x;
      const double dy = b.y - a.y;
      const double len2 = dx * dx + dy * dy;
      const double t =
          len2 == 0 ? 0 : std::max(0.0, std::min(1.0, ((p.x - a.x) * dx + (p.y - a.y) * dy) / len2));
      nearest = std::min(nearest, std::hypot(p.x - (a.x + t * dx), p.y - (a.y + t * dy)));
    }
  }
  return inside ? nearest : -nearest;
}

}  // namespace

int main() {
  const polygon square{{{0, 0}, {10, 0}, {10, 10}, {0, 10}, {0, 0}}};
  check(mapbox::polylabel(square, 1.0) == point(5, 5), "square");

  // The hole covers the outer ring's centroid (50, 50).  The widest circle
  // sits in the far corner, touching two outer edges and the hole's corner
  // (60, 60): radius 40 * (2 - sqrt 2) at x = y = 100 - radius.
  const polygon framed{
      {{0, 0}, {100, 0}, {100, 100}, {0, 100}, {0, 0}},
      {{10, 10}, {10, 60}, {60, 60}, {60, 10}, {10, 10}},
  };
  const double radius = 40 * (2 - std::sqrt(2.0));
  check(signed_distance(point(50, 50), framed) == -10, "the centroid lies in the hole");
  // Precision 1 stops 0.0085 short of the radius, so this bound needs the
  // finer search.
  const point fine = mapbox::polylabel(framed, 0.001);
  const double fine_d = signed_distance(fine, framed);
  check(fine_d >= radius - 0.001 && fine_d <= radius + 1e-9, "framed: distance within precision");
  check(std::fabs(fine.x - (100 - radius)) < 0.005 && std::fabs(fine.y - (100 - radius)) < 0.005,
        "framed: the far corner's pole");
  // With the default precision (1):
  const double coarse_d = signed_distance(mapbox::polylabel(framed), framed);
  check(coarse_d >= radius - 1 && coarse_d <= radius + 1e-9, "framed: default precision");

  // A 30x10 frame starts as three 10x10 cells; only the right one can hold
  // a circle of radius 5.
  const polygon wide{
      {{0, 0}, {30, 0}, {30, 10}, {0, 10}, {0, 0}},
      {{2, 2}, {2, 8}, {18, 8}, {18, 2}, {2, 2}},
  };
  check(signed_distance(mapbox::polylabel(wide), wide) >= 5 - 1e-9, "wide frame");

  // Upstream's degenerate cases: a flat ring, and one that retraces itself.
  check(mapbox::polylabel(polygon{{{0, 0}, {1, 0}, {2, 0}, {0, 0}}}) == point(0, 0), "flat ring");
  check(mapbox::polylabel(polygon{{{0, 0}, {1, 0}, {1, 1}, {1, 0}, {0, 0}}}) == point(0, 0),
        "retraced ring");

  std::printf("mapbox-polylabel (C++): %s\n", ok ? "ok" : "FAILED");
  return ok ? 0 : 1;
}
