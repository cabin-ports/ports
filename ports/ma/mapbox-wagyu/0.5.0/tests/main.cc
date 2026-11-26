// Clips two overlapping integer squares (the shapes of upstream's
// tests/unit/vatti.cpp) with every clip type, and box-clips a square through
// quick_clip.hpp.  Areas are measured independently of the library.
#include <mapbox/geometry/wagyu/quick_clip.hpp>
#include <mapbox/geometry/wagyu/wagyu.hpp>

#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace {

namespace geo = mapbox::geometry;
namespace wg = mapbox::geometry::wagyu;
using ring = geo::linear_ring<std::int64_t>;
using polygon = geo::polygon<std::int64_t>;
using multi = geo::multi_polygon<std::int64_t>;

bool ok = true;

void check(bool pass, const char* what) {
  if (!pass) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ok = false;
  }
}

std::int64_t ring_area(const ring& r) {  // shoelace, unsigned
  std::int64_t twice = 0;
  for (std::size_t i = 0; i + 1 < r.size(); ++i)
    twice += r[i].x * r[i + 1].y - r[i + 1].x * r[i].y;
  return std::llabs(twice) / 2;
}

std::int64_t total_area(const multi& m) {
  std::int64_t sum = 0;
  for (const auto& p : m)
    for (const auto& r : p) sum += ring_area(r);
  return sum;
}

ring square(std::int64_t lo, std::int64_t hi) {
  return {{lo, lo}, {hi, lo}, {hi, hi}, {lo, hi}, {lo, lo}};
}

multi run(wg::clip_type type, bool* executed) {
  wg::wagyu<std::int64_t> w;
  w.add_polygon(polygon{square(0, 10)}, wg::polygon_type_subject);
  w.add_polygon(polygon{square(5, 15)}, wg::polygon_type_clip);
  multi solution;
  *executed = w.execute(type, solution, wg::fill_type_even_odd,
                        wg::fill_type_even_odd);
  return solution;
}

}  // namespace

int main() {
  bool ran = false;

  multi inter = run(wg::clip_type_intersection, &ran);
  check(ran, "intersection executes");
  check(inter.size() == 1 && inter[0].size() == 1, "intersection: one ring");
  if (inter.size() == 1 && inter[0].size() == 1) {
    const ring& r = inter[0][0];
    check(r.size() == 5 && r.front() == r.back(), "intersection: closed quad");
    check(ring_area(r) == 25, "intersection area 25");
    bool inside = true;
    for (const auto& p : r)
      inside = inside && (p.x == 5 || p.x == 10) && (p.y == 5 || p.y == 10);
    check(inside, "intersection vertices on the 5..10 corners");
  }

  multi uni = run(wg::clip_type_union, &ran);
  check(ran && uni.size() == 1 && uni[0].size() == 1, "union: one ring");
  check(total_area(uni) == 175, "union area 175");

  multi diff = run(wg::clip_type_difference, &ran);
  check(ran && diff.size() == 1, "difference: one polygon");
  check(total_area(diff) == 75, "difference area 75");

  multi x = run(wg::clip_type_x_or, &ran);
  check(ran, "xor executes");
  check(total_area(x) == 150, "xor area 150");

  wg::wagyu<std::int64_t> empty;
  multi none;
  check(!empty.execute(wg::clip_type_union, none, wg::fill_type_even_odd,
                       wg::fill_type_even_odd),
        "empty wagyu executes to false");

  // quick_clip: the 0..10 square cut to the box (2,2)-(8,8).
  multi clipped = wg::clip(polygon{square(0, 10)},
                           geo::box<std::int64_t>({2, 2}, {8, 8}),
                           wg::fill_type_even_odd);
  check(clipped.size() == 1 && clipped[0].size() == 1, "clip: one ring");
  check(total_area(clipped) == 36, "clip area 36");

  if (ok) std::puts("ok");
  return ok ? 0 : 1;
}
