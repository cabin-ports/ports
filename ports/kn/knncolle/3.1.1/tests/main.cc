// Consumer test for cabin-ports/knncolle: exact k-nearest-neighbor
// searches with the brute-force and VP-tree indices under Euclidean and
// Manhattan distances, checked against hand-computed neighbors and an
// independent exhaustive search; find_nearest_neighbors() on several
// threads through subpar; query-point and range (search_all) searches;
// neighbors by angle through L2NormalizedBuilder; cap_k(); a float and
// std::size_t instantiation; and a VP tree saved to disk and loaded back
// through the registries, which refuse an unregistered algorithm.
#include "knncolle/knncolle.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <random>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char* what) {
  if (!ok) {
    std::printf("FAILED: %s\n", what);
    ++failures;
  }
}

bool near(double a, double b) { return std::abs(a - b) <= 1e-9 * (1 + std::abs(b)); }

using knncolle::find_nearest_neighbors;
using Neighbors = std::vector<std::pair<int, double>>;

bool same(const Neighbors& got, const Neighbors& want) {
  if (got.size() != want.size())
    return false;
  for (std::size_t j = 0; j < got.size(); ++j)
    if (got[j].first != want[j].first || !near(got[j].second, want[j].second))
      return false;
  return true;
}

bool same(const knncolle::NeighborList<int, double>& got, const std::vector<Neighbors>& want) {
  if (got.size() != want.size())
    return false;
  for (std::size_t i = 0; i < got.size(); ++i)
    if (!same(got[i], want[i]))
      return false;
  return true;
}

// Five 2-D points, stored column-major (dimensions x observations).  Under
// both metrics, each point's distances to the others are distinct.
const std::vector<double> points = {0, 0, 1, 0, 0, 2, 5, 0, 0, -7};

template <class Builder>
std::unique_ptr<knncolle::Prebuilt<int, double, double>> build(
    std::shared_ptr<const knncolle::DistanceMetric<double, double>> metric, int ndim, int nobs,
    const double* data) {
  knncolle::SimpleMatrix<int, double> mat(ndim, nobs, data);
  return Builder(std::move(metric)).build_unique(mat);
}

using Brute = knncolle::BruteforceBuilder<int, double, double>;
using Vptree = knncolle::VptreeBuilder<int, double, double>;

std::shared_ptr<const knncolle::DistanceMetric<double, double>> euclidean() {
  return std::make_shared<knncolle::EuclideanDistance<double, double>>();
}

std::shared_ptr<const knncolle::DistanceMetric<double, double>> manhattan() {
  return std::make_shared<knncolle::ManhattanDistance<double, double>>();
}

// The k nearest neighbors of every observation by exhaustive search,
// computed here independently of knncolle.
std::vector<Neighbors> reference(const std::vector<double>& data, int ndim, int k, bool l1) {
  const int nobs = static_cast<int>(data.size()) / ndim;
  std::vector<Neighbors> out(nobs);
  for (int i = 0; i < nobs; ++i) {
    std::vector<std::pair<double, int>> all;
    for (int j = 0; j < nobs; ++j) {
      if (j == i)
        continue;
      double d = 0;
      for (int c = 0; c < ndim; ++c) {
        const double delta = data[i * ndim + c] - data[j * ndim + c];
        d += l1 ? std::abs(delta) : delta * delta;
      }
      all.emplace_back(l1 ? d : std::sqrt(d), j);
    }
    std::sort(all.begin(), all.end());
    for (int j = 0; j < k; ++j)
      out[i].emplace_back(all[j].second, all[j].first);
  }
  return out;
}

void hand_computed() {
  const std::vector<Neighbors> l2 = {
      {{1, 1}, {2, 2}},
      {{0, 1}, {2, std::sqrt(5.0)}},
      {{0, 2}, {1, std::sqrt(5.0)}},
      {{1, 4}, {0, 5}},
      {{0, 7}, {1, std::sqrt(50.0)}},
  };
  const std::vector<Neighbors> l1 = {
      {{1, 1}, {2, 2}}, {{0, 1}, {2, 3}}, {{0, 2}, {1, 3}}, {{1, 4}, {0, 5}}, {{0, 7}, {1, 8}},
  };
  check(same(find_nearest_neighbors(*build<Brute>(euclidean(), 2, 5, points.data()), 2), l2),
        "brute force, Euclidean");
  check(same(find_nearest_neighbors(*build<Vptree>(euclidean(), 2, 5, points.data()), 2), l2),
        "VP tree, Euclidean");
  check(same(find_nearest_neighbors(*build<Brute>(manhattan(), 2, 5, points.data()), 2), l1),
        "brute force, Manhattan");
  check(same(find_nearest_neighbors(*build<Vptree>(manhattan(), 2, 5, points.data()), 2), l1),
        "VP tree, Manhattan");

  auto vp = build<Vptree>(euclidean(), 2, 5, points.data());
  check(vp->num_observations() == 5 && vp->num_dimensions() == 2, "the index's shape");

  // k above the number of other observations is capped.
  auto capped = find_nearest_neighbors(*vp, 10);
  bool all_four = capped.size() == 5;
  for (const auto& n : capped)
    all_four = all_four && n.size() == 4;
  check(all_four, "find_nearest_neighbors caps k at nobs - 1");
  check(knncolle::cap_k(10, 5) == 4 && knncolle::cap_k(3, 5) == 3 && knncolle::cap_k(3, 0) == 0 &&
            knncolle::cap_k_query(10, 5) == 5,
        "cap_k and cap_k_query");
  check(knncolle::find_nearest_neighbors_index_only(*vp, 2) ==
            std::vector<std::vector<int>>{{1, 2}, {0, 2}, {0, 1}, {1, 0}, {0, 1}},
        "find_nearest_neighbors_index_only");

  // A query point is not one of the observations, so nothing is excluded.
  auto brute = build<Brute>(euclidean(), 2, 5, points.data());
  for (const auto* index : {vp.get(), brute.get()}) {
    auto searcher = index->initialize();
    const double query[2] = {0.9, 0.1};
    std::vector<int> indices;
    std::vector<double> distances;
    searcher->search(query, 3, &indices, &distances);
    check(indices == std::vector<int>{1, 0, 2} && near(distances[0], std::sqrt(0.02)) &&
              near(distances[1], std::sqrt(0.82)) && near(distances[2], std::sqrt(4.42)),
          "a query point's nearest neighbors");
    searcher->search(4, 1, &indices, nullptr);
    check(indices == std::vector<int>{0}, "an observation's nearest neighbor, indices only");

    check(searcher->can_search_all(), "range search is supported");
    check(searcher->search_all(0, 2.5, &indices, &distances) == 2 &&
              indices == std::vector<int>{1, 2} && near(distances[1], 2),
          "observations within range of an observation, excluding itself");
    check(searcher->search_all(0, 2.0, &indices, nullptr) == 2 &&
              indices == std::vector<int>{1, 2},
          "the range includes a point exactly at its bound");
    check(searcher->search_all(query, 2.2, &indices, nullptr) == 3 &&
              indices == std::vector<int>{1, 0, 2},
          "observations within range of a query point");
    check(searcher->search_all(query, 0.1, nullptr, nullptr) == 0, "an empty range, counted only");
  }
}

// A deterministic pseudo-random dataset, large enough for a deep tree.
std::vector<double> random_data(int count) {
  std::vector<double> out;
  std::uint64_t state = 42;
  for (int i = 0; i < count; ++i) {
    state = state * 6364136223846793005ULL + 1442695040888963407ULL;
    out.push_back(static_cast<double>(state >> 11) / 9007199254740992.0);
  }
  return out;
}

void against_reference() {
  const int ndim = 4, nobs = 300, k = 8;
  const auto data = random_data(ndim * nobs);
  auto vp = build<Vptree>(euclidean(), ndim, nobs, data.data());
  const auto want = reference(data, ndim, k, false);
  const auto serial = find_nearest_neighbors(*vp, k);
  check(same(serial, want), "the VP tree matches an exhaustive Euclidean search");
  check(same(find_nearest_neighbors(*vp, k, 3), want),
        "three threads give the same neighbors as one");
  auto vp_l1 = build<Vptree>(manhattan(), ndim, nobs, data.data());
  check(same(find_nearest_neighbors(*vp_l1, k, 2), reference(data, ndim, k, true)),
        "the VP tree matches an exhaustive Manhattan search");

  // Every observation's range count matches the brute-force index's.
  auto brute = build<Brute>(euclidean(), ndim, nobs, data.data());
  auto vs = vp->initialize();
  auto bs = brute->initialize();
  bool counts = true;
  int total = 0;
  for (int i = 0; i < nobs; ++i) {
    const int c = vs->search_all(i, 0.3, nullptr, nullptr);
    counts = counts && c == bs->search_all(i, 0.3, nullptr, nullptr);
    total += c;
  }
  check(counts && total > 0, "range counts agree between the VP tree and brute force");

  // Saving the index and loading it back gives the same searches.
  std::random_device seed;
  std::filesystem::path dir;
  do
    dir = std::filesystem::temp_directory_path() / ("knncolle-test-" + std::to_string(seed()));
  while (!std::filesystem::create_directory(dir));
  std::filesystem::create_directories(dir / "vptree");
  std::filesystem::create_directories(dir / "brute");
  vp->save(dir / "vptree");
  brute->save(dir / "brute");

  knncolle::register_load_euclidean_distance<double, double>();
  auto& registry = knncolle::load_prebuilt_registry<int, double, double>();
  registry[knncolle::vptree_prebuilt_save_name] = [](const std::filesystem::path& path) {
    return knncolle::load_vptree_prebuilt<int, double, double>(path);
  };
  auto reloaded = knncolle::load_prebuilt_unique<int, double, double>(dir / "vptree");
  check(reloaded->num_observations() == nobs && reloaded->num_dimensions() == ndim,
        "the reloaded index's shape");
  check(same(find_nearest_neighbors(*reloaded, k), want),
        "the reloaded VP tree finds the same neighbors");

  std::string missing;
  try {
    knncolle::load_prebuilt_unique<int, double, double>(dir / "brute");
  } catch (const knncolle::LoadPrebuiltNotFoundError& e) {
    missing = e.get_algorithm();
  }
  check(missing == knncolle::bruteforce_prebuilt_save_name,
        "loading an unregistered algorithm throws");
  std::error_code ignored;
  std::filesystem::remove_all(dir, ignored);
}

void cosine() {
  // (1, 0) and (3, 0) point the same way; (0, 2) is closer in angle to
  // (2, 1) than to either.
  const std::vector<double> data = {1, 0, 3, 0, 0, 2, 2, 1};
  knncolle::SimpleMatrix<int, double> mat(2, 4, data.data());
  knncolle::L2NormalizedBuilder<int, double, double, double> builder(
      std::make_shared<Vptree>(euclidean()));
  auto index = builder.build_unique(mat);
  auto searcher = index->initialize();
  std::vector<int> indices;
  std::vector<double> distances;
  searcher->search(0, 1, &indices, &distances);
  check(indices == std::vector<int>{1} && near(distances[0], 0),
        "parallel vectors are at distance 0");
  searcher->search(2, 1, &indices, &distances);
  check(indices == std::vector<int>{3} && near(distances[0], std::sqrt(2 - 2 / std::sqrt(5.0))),
        "the nearest neighbor by angle");
  // Unnormalized, the query would be about 4.13 from the normalized (2, 1).
  const double query[2] = {3, 4};
  searcher->search(query, 2, &indices, &distances);
  check(indices == std::vector<int>{3, 2} &&
            near(distances[0], std::sqrt(2 - 4 / std::sqrt(5.0))) &&
            near(distances[1], std::sqrt(0.4)),
        "a query is normalized too");
}

void float_size_t() {
  const std::vector<float> data(points.begin(), points.end());
  knncolle::SimpleMatrix<std::size_t, float> mat(2, 5, data.data());
  knncolle::VptreeBuilder<std::size_t, float, float> builder(
      std::make_shared<knncolle::EuclideanDistance<float, float>>());
  auto found = find_nearest_neighbors(*builder.build_unique(mat), 2);
  check(found[3] == std::vector<std::pair<std::size_t, float>>{{1, 4.0f}, {0, 5.0f}},
        "a float and std::size_t VP tree");
  check(knncolle::get_numeric_type<float>() == knncolle::NumericType::FLOAT &&
            knncolle::get_numeric_type<double>() == knncolle::NumericType::DOUBLE,
        "get_numeric_type");
}

}  // namespace

int main() {
  hand_computed();
  against_reference();
  cosine();
  float_size_t();
  std::printf("knncolle (C++): %s\n", failures == 0 ? "ok" : "FAILED");
  return failures == 0 ? 0 : 1;
}
