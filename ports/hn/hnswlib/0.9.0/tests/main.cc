// hnswlib: the L2, inner-product and integer L2 distances are exact for each
// kernel width; an HNSW index searched with ef >= its size answers like brute
// force, with label filters and deleted elements; it survives a save and a
// load and grows with resizeIndex; misuse throws.
#include <hnswlib/hnswlib.h>

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#  define NOMINMAX
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#else
#  include <unistd.h>
#endif

namespace {

int failures = 0;

void check(bool ok, int line, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "main.cc:%d: check failed: %s\n", line, what);
    ++failures;
  }
}

#define CHECK(expr) check((expr), __LINE__, #expr)

#define CHECK_THROWS(stmt)                       \
  do {                                           \
    bool thrown = false;                         \
    try {                                        \
      stmt;                                      \
    } catch (const std::runtime_error&) {        \
      thrown = true;                             \
    }                                            \
    check(thrown, __LINE__, #stmt " throws");    \
  } while (0)

typedef std::vector<std::pair<float, hnswlib::labeltype> > results;

// Reserves a new file in the temporary directory for saveIndex to rewrite.
bool temp_path(std::string& path) {
#ifdef _WIN32
  char dir[MAX_PATH + 1];
  char name[MAX_PATH];
  if (GetTempPathA(MAX_PATH + 1, dir) == 0 ||
      GetTempFileNameA(dir, "hns", 0, name) == 0) {
    return false;
  }
  path = name;
  return true;
#else
  const char* dir = std::getenv("TMPDIR");
  path = std::string(dir && *dir ? dir : "/tmp") + "/hnswlib-test-XXXXXX";
  const int fd = mkstemp(&path[0]);
  return fd >= 0 && close(fd) == 0;
#endif
}

struct OddLabels : hnswlib::BaseFilterFunctor {
  bool operator()(hnswlib::labeltype label) override { return label % 2 == 1; }
};

bool sorted(const results& r) {
  for (std::size_t i = 1; i < r.size(); ++i) {
    if (r[i - 1].first > r[i].first) {
      return false;
    }
  }
  return true;
}

// The float spaces pick a kernel from the dimension: 16 lanes, 4 lanes, each
// with a scalar tail above 16 and above 4, or scalar code.  The values are
// small multiples of 0.5, so every summation order gives the exact result.
void check_distances() {
  const std::size_t dims[] = {3, 7, 8, 16, 21};
  for (std::size_t n = 0; n < sizeof dims / sizeof dims[0]; ++n) {
    const std::size_t dim = dims[n];
    std::vector<float> a(dim), b(dim);
    float l2 = 0.0f, dot = 0.0f;
    for (std::size_t i = 0; i < dim; ++i) {
      a[i] = static_cast<float>(i % 5) - 2.0f;
      b[i] = static_cast<float>(i % 3) * 0.5f;
      l2 += (a[i] - b[i]) * (a[i] - b[i]);
      dot += a[i] * b[i];
    }
    hnswlib::L2Space l2_space(dim);
    hnswlib::InnerProductSpace ip_space(dim);
    const bool l2_ok = l2_space.get_dist_func()(
        a.data(), b.data(), l2_space.get_dist_func_param()) == l2;
    const bool ip_ok = ip_space.get_dist_func()(
        a.data(), b.data(), ip_space.get_dist_func_param()) == 1.0f - dot;
    if (!l2_ok || !ip_ok) {
      std::fprintf(stderr, "main.cc: wrong distance for dimension %u\n",
                   static_cast<unsigned>(dim));
      ++failures;
    }
    CHECK(l2_space.get_data_size() == dim * sizeof(float));
  }

  // Bytes, four at a time or one at a time.
  const std::size_t byte_dims[] = {5, 8};
  for (std::size_t n = 0; n < 2; ++n) {
    const std::size_t dim = byte_dims[n];
    std::vector<unsigned char> a(dim), b(dim);
    int expected = 0;
    for (std::size_t i = 0; i < dim; ++i) {
      a[i] = static_cast<unsigned char>(i * 37 % 256);
      b[i] = static_cast<unsigned char>(250 - i * 11);
      expected += (a[i] - b[i]) * (a[i] - b[i]);
    }
    hnswlib::L2SpaceI space(dim);
    CHECK(space.get_dist_func()(a.data(), b.data(),
                                space.get_dist_func_param()) == expected);
  }
}

}  // namespace

int main() {
  check_distances();

  const std::size_t dim = 16;
  const std::size_t count = 200;
  const std::size_t queries = 10;
  const std::size_t k = 5;

  // std::mt19937's output is fixed by the standard; the distributions are not.
  std::mt19937 rng(2024);
  std::vector<float> data(count * dim);
  std::vector<float> query(queries * dim);
  for (std::size_t i = 0; i < data.size(); ++i) {
    data[i] = static_cast<float>(rng() >> 8) / 16777216.0f;
  }
  for (std::size_t i = 0; i < query.size(); ++i) {
    query[i] = static_cast<float>(rng() >> 8) / 16777216.0f;
  }

  hnswlib::L2Space space(dim);
  hnswlib::HierarchicalNSW<float> index(&space, count, 16, 200);
  hnswlib::BruteforceSearch<float> exact(&space, count);
  for (std::size_t i = 0; i < count; ++i) {
    index.addPoint(&data[i * dim], i);
    exact.addPoint(&data[i * dim], i);
  }
  CHECK(index.getCurrentElementCount() == count);
  CHECK(index.getMaxElements() == count);
  CHECK_THROWS(index.addPoint(&query[0], count));

  // With ef at least the element count the search visits every element
  // reachable at level 0, which in this graph is all of them.
  index.setEf(count);
  bool self_found = true;
  for (std::size_t i = 0; i < count; ++i) {
    const results r = index.searchKnnCloserFirst(&data[i * dim], 1);
    self_found = self_found && r.size() == 1 && r[0].first == 0.0f &&
                 r[0].second == i;
  }
  CHECK(self_found);

  OddLabels odd;
  for (std::size_t q = 0; q < queries; ++q) {
    const float* p = &query[q * dim];
    const results r = index.searchKnnCloserFirst(p, k);
    CHECK(r.size() == k && sorted(r));
    CHECK(r == exact.searchKnnCloserFirst(p, k));
    CHECK(index.searchKnn(p, k).top() == r.back());

    const results only_odd = index.searchKnnCloserFirst(p, k, &odd);
    CHECK(only_odd == exact.searchKnnCloserFirst(p, k, &odd));
    bool all_odd = only_odd.size() == k;
    for (std::size_t i = 0; i < only_odd.size(); ++i) {
      all_odd = all_odd && only_odd[i].second % 2 == 1;
    }
    CHECK(all_odd);
  }

  const std::vector<float> stored = index.getDataByLabel<float>(7);
  CHECK(stored == std::vector<float>(&data[7 * dim], &data[8 * dim]));

  // A deleted element stays in the graph but leaves the results.  The
  // reference is rebuilt, not removed from (see cabin.toml).
  const hnswlib::labeltype nearest =
      index.searchKnnCloserFirst(&query[0], 1)[0].second;
  index.markDelete(nearest);
  hnswlib::BruteforceSearch<float> remaining(&space, count);
  for (std::size_t i = 0; i < count; ++i) {
    if (i != nearest) {
      remaining.addPoint(&data[i * dim], i);
    }
  }
  CHECK(index.getDeletedCount() == 1);
  CHECK(index.searchKnnCloserFirst(&query[0], k) ==
        remaining.searchKnnCloserFirst(&query[0], k));
  CHECK_THROWS(index.getDataByLabel<float>(nearest));
  CHECK_THROWS(index.markDelete(nearest));
  CHECK_THROWS(index.markDelete(count + 1));

  std::string path;
  if (!temp_path(path)) {
    std::fprintf(stderr, "main.cc: cannot create a temporary file\n");
    return 1;
  }
  index.saveIndex(path);
  {
    hnswlib::HierarchicalNSW<float> loaded(&space, path);
    loaded.setEf(count);
    CHECK(loaded.getCurrentElementCount() == count);
    CHECK(loaded.getDeletedCount() == 1);
    bool same = true;
    for (std::size_t q = 0; q < queries; ++q) {
      const float* p = &query[q * dim];
      same = same && loaded.searchKnnCloserFirst(p, k) ==
                         remaining.searchKnnCloserFirst(p, k);
    }
    CHECK(same);
    loaded.unmarkDelete(nearest);
    CHECK(loaded.getDeletedCount() == 0);
    CHECK(loaded.searchKnnCloserFirst(&query[0], 1)[0].second == nearest);
  }
  CHECK(std::remove(path.c_str()) == 0);
  CHECK_THROWS(hnswlib::HierarchicalNSW<float> missing(&space, path));

  // A full index takes more elements once resized.
  index.resizeIndex(count + 10);
  index.setEf(count + 10);
  index.addPoint(&query[0], count);
  const results added = index.searchKnnCloserFirst(&query[0], 1);
  CHECK(added.size() == 1 && added[0].first == 0.0f &&
        added[0].second == count);

  if (failures != 0) {
    return 1;
  }
  std::printf("hnswlib ok\n");
  return 0;
}
