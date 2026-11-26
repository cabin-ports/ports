// C++ consumer: checks size_radiuses and both layouts against values derived
// from the algorithms (circle start, centre and scale to 90% of the frame).
#include <nodesoup.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

static int failures = 0;

static void near(const char* what, double got, double want, double eps) {
  if (!(std::fabs(got - want) < eps)) {
    std::printf("FAIL %s: got %.12g, want %.12g\n", what, got, want);
    ++failures;
  }
}

int main() {
  // Star: vertex 0 has degree 3 of 4, the leaves degree 1.
  const nodesoup::adj_list_t star = {{1, 2, 3}, {0}, {0}, {0}};
  const std::vector<double> r = nodesoup::size_radiuses(star);
  if (r.size() != 4) {
    std::printf("FAIL size_radiuses size %zu\n", r.size());
    return 1;
  }
  near("radius hub", r[0], 4 + std::log2(225.0), 1e-9);
  for (int i = 1; i < 4; ++i) near("radius leaf", r[i], 4 + std::log2(75.0), 1e-9);

  // Two vertices: the circle start puts them at (1,0) and (-1,0); centring
  // and scaling into 100x100 leaves them at +-45 on the x axis.
  const nodesoup::adj_list_t edge = {{1}, {0}};
  const auto fr = nodesoup::fruchterman_reingold(edge, 100, 100);
  const auto kk = nodesoup::kamada_kawai(edge, 100, 100);
  for (const auto* p : {&fr, &kk}) {
    if (p->size() != 2) {
      std::printf("FAIL layout size %zu\n", p->size());
      return 1;
    }
    near("x0", (*p)[0].x, 45, 1e-6);
    near("x1", (*p)[1].x, -45, 1e-6);
    near("y0", (*p)[0].y, 0, 1e-6);
    near("y1", (*p)[1].y, 0, 1e-6);
  }

  // 4-cycle: the box is centred on the origin and its larger side spans 90.
  const nodesoup::adj_list_t cycle = {{1, 3}, {0, 2}, {1, 3}, {0, 2}};
  unsigned calls = 0;
  const auto c = nodesoup::fruchterman_reingold(
      cycle, 100, 100, 10, 15.0,
      [&](const std::vector<nodesoup::Point2D>& pts, int) {
        if (pts.size() == 4) ++calls;
      });
  if (c.size() != 4) {
    std::printf("FAIL cycle size %zu\n", c.size());
    return 1;
  }
  double xmin = c[0].x, xmax = c[0].x, ymin = c[0].y, ymax = c[0].y;
  for (const auto& p : c) {
    xmin = std::min(xmin, p.x);
    xmax = std::max(xmax, p.x);
    ymin = std::min(ymin, p.y);
    ymax = std::max(ymax, p.y);
  }
  near("cycle cx", (xmin + xmax) / 2, 0, 1e-6);
  near("cycle cy", (ymin + ymax) / 2, 0, 1e-6);
  near("cycle side", std::max(xmax - xmin, ymax - ymin), 90, 1e-6);
  if (calls != 10) {
    std::printf("FAIL callback calls %u\n", calls);
    ++failures;
  }

  // 3-path: the circle start is an equilateral triangle, so only the force
  // iterations can make the two edges differ from the far pair's distance.
  const nodesoup::adj_list_t path = {{1}, {0, 2}, {1}};
  const auto fr3 = nodesoup::fruchterman_reingold(path, 100, 100);
  const auto kk3 = nodesoup::kamada_kawai(path, 100, 100);
  for (const auto* p : {&fr3, &kk3}) {
    if (p->size() != 3) {
      std::printf("FAIL path size %zu\n", p->size());
      return 1;
    }
    const auto dist = [&](int a, int b) {
      return std::hypot((*p)[a].x - (*p)[b].x, (*p)[a].y - (*p)[b].y);
    };
    near("path edges equal", dist(0, 1), dist(1, 2), 0.5);
    if (!(dist(0, 2) > 1.5 * dist(0, 1))) {
      std::printf("FAIL path not stretched\n");
      ++failures;
    }
  }

  if (failures == 0) std::printf("ok\n");
  return failures == 0 ? 0 : 1;
}
