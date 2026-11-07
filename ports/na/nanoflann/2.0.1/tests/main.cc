// The C++ consumer: nanoflann in C++11 - a static KD-tree over a 3-D
// point cloud adaptor, with k-nearest searches under the L2 and L1 metrics
// and L2 radius searches checked against brute force, the radius's
// exclusive bound, a parallel build that must agree with the serial one,
// and the dynamic and incremental indices adding and removing points.
#include <nanoflann.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

static int failures = 0;

#define CHECK(cond)                                                   \
  do {                                                                \
    if (!(cond)) {                                                    \
      std::fprintf(stderr, "main.cc:%d: check failed: %s\n", __LINE__, \
                   #cond);                                            \
      ++failures;                                                     \
    }                                                                 \
  } while (0)

namespace {

struct Point {
  double c[3];
};

// The dataset interface nanoflann's adaptors query.
struct Cloud {
  std::vector<Point> pts;
  std::size_t kdtree_get_point_count() const { return pts.size(); }
  double kdtree_get_pt(std::size_t i, std::size_t dim) const {
    return pts[i].c[dim];
  }
  template <class BBox>
  bool kdtree_get_bbox(BBox&) const {
    return false;
  }
};

std::uint32_t state = 12345;

double coord() {  // deterministic, in [0, 10)
  state = state * 1664525u + 1013904223u;
  return (state >> 8) / 16777216.0 * 10.0;
}

Point random_point() {
  Point p;
  for (double& v : p.c) v = coord();
  return p;
}

double l2(const Point& a, const Point& b) {  // squared, as nanoflann's L2
  double d = 0;
  for (int i = 0; i < 3; ++i) d += (a.c[i] - b.c[i]) * (a.c[i] - b.c[i]);
  return d;
}

double l1(const Point& a, const Point& b) {
  double d = 0;
  for (int i = 0; i < 3; ++i) d += std::fabs(a.c[i] - b.c[i]);
  return d;
}

bool close_to(double a, double b) { return std::fabs(a - b) <= 1e-9; }

using L2Tree = nanoflann::KDTreeSingleIndexAdaptor<
    nanoflann::L2_Simple_Adaptor<double, Cloud>, Cloud, 3>;
using L1Tree =
    nanoflann::KDTreeSingleIndexAdaptor<nanoflann::L1_Adaptor<double, Cloud>,
                                        Cloud, 3>;

// A k-nearest search returns the k smallest distances in ascending order,
// each the true distance of the index it names.
template <class Tree, class Metric>
void check_knn(const Tree& tree, const Cloud& cloud,
               const std::vector<bool>& active, const Point& q,
               std::size_t k, Metric metric) {
  std::vector<std::uint32_t> idx(k);
  std::vector<double> dist(k);
  nanoflann::KNNResultSet<double, std::uint32_t> result(k);
  result.init(idx.data(), dist.data());
  tree.findNeighbors(result, q.c);
  CHECK(result.size() == k);

  std::vector<double> all;
  for (std::size_t i = 0; i < cloud.pts.size(); ++i)
    if (active[i]) all.push_back(metric(cloud.pts[i], q));
  std::sort(all.begin(), all.end());
  for (std::size_t i = 0; i < k; ++i) {
    CHECK(active[idx[i]]);
    CHECK(close_to(dist[i], metric(cloud.pts[idx[i]], q)));
    CHECK(close_to(dist[i], all[i]));
  }
}

void static_index() {
  // 4000 points, so that the parallel build hands subtrees of more than
  // 512 points (its cutoff) to std::async tasks on several levels.
  Cloud cloud;
  for (int i = 0; i < 4000; ++i) cloud.pts.push_back(random_point());
  const std::vector<bool> active(cloud.pts.size(), true);

  L2Tree tree(3, cloud, nanoflann::KDTreeSingleIndexAdaptorParams(8));
  L1Tree l1_tree(3, cloud, nanoflann::KDTreeSingleIndexAdaptorParams(8));
  L2Tree parallel(3, cloud,
                  nanoflann::KDTreeSingleIndexAdaptorParams(
                      8, nanoflann::KDTreeSingleIndexAdaptorFlags::None, 4));

  for (int n = 0; n < 50; ++n) {
    const Point q = random_point();
    check_knn(tree, cloud, active, q, 7, l2);
    check_knn(l1_tree, cloud, active, q, 7, l1);

    std::uint32_t a[7], b[7];
    double da[7], db[7];
    CHECK(tree.knnSearch(q.c, 7, a, da) == 7);
    CHECK(parallel.knnSearch(q.c, 7, b, db) == 7);
    CHECK(std::equal(a, a + 7, b) && std::equal(da, da + 7, db));

    // radiusSearch takes and returns squared distances under L2, keeps only
    // those strictly inside the radius, and sorts them by default.
    const double radius = 1.5;
    std::vector<nanoflann::ResultItem<std::uint32_t, double>> found;
    const std::size_t count = tree.radiusSearch(q.c, radius, found);
    std::vector<std::uint32_t> expected, got;
    for (std::uint32_t i = 0; i < cloud.pts.size(); ++i)
      if (l2(cloud.pts[i], q) < radius) expected.push_back(i);
    CHECK(count == found.size());
    for (std::size_t i = 0; i < found.size(); ++i) {
      CHECK(close_to(found[i].second, l2(cloud.pts[found[i].first], q)));
      CHECK(i == 0 || found[i - 1].second <= found[i].second);
      got.push_back(found[i].first);
    }
    std::sort(got.begin(), got.end());
    CHECK(got == expected);
  }

  // A dataset point is its own nearest neighbor, at distance zero.
  std::uint32_t self = 0;
  double self_dist = -1;
  CHECK(tree.knnSearch(cloud.pts[123].c, 1, &self, &self_dist) == 1);
  CHECK(self == 123 && self_dist == 0);
}

void radius_boundary() {
  // A point exactly on the radius is left out: the bound is exclusive.
  Cloud cloud;
  for (int i = 0; i < 3; ++i) {
    Point p = {{static_cast<double>(i), 0, 0}};
    cloud.pts.push_back(p);
  }
  L2Tree tree(3, cloud, nanoflann::KDTreeSingleIndexAdaptorParams(8));
  std::vector<nanoflann::ResultItem<std::uint32_t, double>> found;
  const double origin[3] = {0, 0, 0};
  CHECK(tree.radiusSearch(origin, 1.0, found) == 1 && found[0].first == 0);
}

void dynamic_index() {
  using DynamicTree = nanoflann::KDTreeSingleIndexDynamicAdaptor<
      nanoflann::L2_Simple_Adaptor<double, Cloud>, Cloud, 3>;
  Cloud cloud;
  DynamicTree tree(3, cloud, nanoflann::KDTreeSingleIndexAdaptorParams(8));
  std::vector<bool> active;

  // Points join in batches; addPoints takes an inclusive index range.
  for (int batch = 0; batch < 3; ++batch) {
    const std::size_t first = cloud.pts.size();
    for (int i = 0; i < 300; ++i) cloud.pts.push_back(random_point());
    active.resize(cloud.pts.size(), true);
    tree.addPoints(static_cast<std::uint32_t>(first),
                   static_cast<std::uint32_t>(cloud.pts.size() - 1));
    check_knn(tree, cloud, active, random_point(), 5, l2);
  }

  // A removed point is no longer found, even searching from its position.
  const Point removed = cloud.pts[417];
  tree.removePoint(417);
  active[417] = false;
  check_knn(tree, cloud, active, removed, 5, l2);
  for (int n = 0; n < 20; ++n)
    check_knn(tree, cloud, active, random_point(), 5, l2);
}

void incremental_index() {
  using IncrementalTree = nanoflann::KDTreeSingleIndexIncrementalAdaptor<
      nanoflann::L2_Simple_Adaptor<double, Cloud>, Cloud, 3>;
  Cloud cloud;
  IncrementalTree tree(3, cloud);

  // An empty tree takes a batch as one balanced build; a batch small next
  // to the tree is inserted point by point.
  for (int i = 0; i < 1000; ++i) cloud.pts.push_back(random_point());
  tree.addPoints(0, 999);
  for (int i = 0; i < 50; ++i) cloud.pts.push_back(random_point());
  tree.addPoints(1000, 1049);
  std::vector<bool> active(cloud.pts.size(), true);
  check_knn(tree, cloud, active, random_point(), 5, l2);
  // Points of either batch are found from their own position.
  check_knn(tree, cloud, active, cloud.pts[17], 5, l2);
  check_knn(tree, cloud, active, cloud.pts[1000], 5, l2);
  check_knn(tree, cloud, active, cloud.pts[1049], 5, l2);

  const Point removed = cloud.pts[321];
  tree.removePoint(321);
  active[321] = false;
  check_knn(tree, cloud, active, removed, 5, l2);

  // removeOutsideBox keeps only the points inside an axis-aligned box.
  IncrementalTree::BoundingBox keep;
  for (auto& side : keep) {
    side.low = 2;
    side.high = 8;
  }
  tree.removeOutsideBox(keep);
  for (std::size_t i = 0; i < cloud.pts.size(); ++i)
    for (double v : cloud.pts[i].c)
      if (v < 2 || v > 8) active[i] = false;
  for (int n = 0; n < 20; ++n)
    check_knn(tree, cloud, active, random_point(), 5, l2);
}

}  // namespace

int main() {
  static_index();
  radius_boundary();
  dynamic_index();
  incremental_index();
  if (failures != 0) return 1;
  std::puts("nanoflann C++ consumer test passed");
  return 0;
}
