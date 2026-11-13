// Checks FLANN's C++ API: exact searches over randomized kd-trees and a
// linear index, and an index saved to and loaded from a file, which goes
// through FLANN's lz4-compressed serialization.  Distances are squared
// Euclidean.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include <flann/flann.hpp>

namespace {

int failures = 0;

void expect(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "failed: %s\n", what);
    ++failures;
  }
}

bool near(float a, float b) { return std::fabs(a - b) < 1e-4f; }

typedef flann::L2<float> Distance;

// The two nearest neighbours of (0.9, 0.1) are points 1 and 0, and of
// (5.8, 5.1) points 4 and 3.
bool expected_neighbours(const flann::Matrix<int>& indices,
                         const flann::Matrix<float>& dists) {
  return indices[0][0] == 1 && indices[0][1] == 0 && indices[1][0] == 4 &&
         indices[1][1] == 3 && near(dists[0][0], 0.02f) &&
         near(dists[0][1], 0.82f) && near(dists[1][0], 0.05f) &&
         near(dists[1][1], 0.65f);
}

// A saved index does not hold the dataset by default, so the loaded copy is
// given it again, as Index's SavedIndexParams constructor does.  The file is
// among the probe's build outputs: MSVC documents tmpfile() as creating its
// file in the drive root.
bool round_trips_through_a_file(const flann::Matrix<float>& dataset,
                                const flann::Matrix<float>& queries,
                                flann::Matrix<int> indices,
                                flann::Matrix<float> dists) {
  const char* const dir = std::getenv("CABIN_PROFILE_DIR");
  if (dir == NULL) return false;
  const std::string path = std::string(dir) + "/port-probe-flann.tmp";
  std::FILE* file = std::fopen(path.c_str(), "w+b");
  if (file == NULL) return false;
  flann::KDTreeSingleIndex<Distance> single(dataset);
  single.buildIndex();
  single.saveIndex(file);
  std::rewind(file);
  flann::KDTreeSingleIndex<Distance> loaded(dataset);
  loaded.loadIndex(file);
  std::fclose(file);
  std::remove(path.c_str());
  loaded.knnSearch(queries, indices, dists, 2, flann::SearchParams());
  return loaded.size() == 5 && expected_neighbours(indices, dists);
}

}  // namespace

int main() {
  float points[] = {0, 0, 1, 0, 0, 1, 5, 5, 6, 5};
  float query_points[] = {0.9f, 0.1f, 5.8f, 5.1f};
  flann::Matrix<float> dataset(points, 5, 2);
  flann::Matrix<float> queries(query_points, 2, 2);

  std::vector<int> index_buffer(4);
  std::vector<float> dist_buffer(4);
  flann::Matrix<int> indices(&index_buffer[0], 2, 2);
  flann::Matrix<float> dists(&dist_buffer[0], 2, 2);

  // Unlimited checks make the randomized kd-tree search exact, for which
  // kdtree_index.h wants a single tree.
  flann::Index<Distance> kdtree(dataset, flann::KDTreeIndexParams(1));
  kdtree.buildIndex();
  expect(kdtree.size() == 5 && kdtree.veclen() == 2, "index size");
  kdtree.knnSearch(queries, indices, dists, 2,
                   flann::SearchParams(flann::FLANN_CHECKS_UNLIMITED));
  expect(expected_neighbours(indices, dists), "kd-tree k-nearest search");

  flann::Index<Distance> linear(dataset, flann::LinearIndexParams());
  linear.buildIndex();
  flann::Matrix<float> origin(points, 1, 2);
  std::vector<std::vector<int> > found;
  std::vector<std::vector<float> > found_dists;
  int count = linear.radiusSearch(origin, found, found_dists, 1.5f,
                                  flann::SearchParams());
  expect(count == 3 && found[0].size() == 3 && found[0][0] == 0 &&
             found_dists[0][0] == 0,
         "linear radius search, sorted");

  std::fill(index_buffer.begin(), index_buffer.end(), -1);
  std::fill(dist_buffer.begin(), dist_buffer.end(), -1.0f);
  expect(round_trips_through_a_file(dataset, queries, indices, dists),
         "saved and loaded single kd-tree");

  if (failures != 0) return 1;
  std::puts("flann C++ API OK");
  return 0;
}
