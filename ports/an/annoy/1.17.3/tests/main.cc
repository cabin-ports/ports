// annoy: the Euclidean, Manhattan, angular, dot-product and Hamming
// distances are exact on small vectors; a forest searched through every leaf
// returns the exact neighbours, by item and by vector; an index saved and
// memory-mapped back, one loaded by another index, and one built directly in
// a file give the same answers; misuse is reported through error strings.
// C++98, as the headers support.
#include <annoy/annoylib.h>
#include <annoy/kissrandom.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
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

typedef Annoy::AnnoyIndexSingleThreadedBuildPolicy Policy;
typedef Annoy::AnnoyIndex<int, float, Annoy::Euclidean, Annoy::Kiss32Random,
                          Policy>
    EuclideanIndex;

const int kDim = 8;
const int kItems = 300;
const int kTrees = 10;
const int kNeighbours = 10;
// Enough candidates to reach every leaf of every tree.
const int kSearchAll = kItems * kTrees;

// Reserves a new file in the temporary directory for annoy to rewrite.
bool temp_path(std::string& path) {
#ifdef _WIN32
  char dir[MAX_PATH + 1];
  char name[MAX_PATH];
  if (GetTempPathA(MAX_PATH + 1, dir) == 0 ||
      GetTempFileNameA(dir, "ann", 0, name) == 0) {
    return false;
  }
  path = name;
  return true;
#else
  const char* dir = std::getenv("TMPDIR");
  path = std::string(dir && *dir ? dir : "/tmp") + "/annoy-test-XXXXXX";
  const int fd = mkstemp(&path[0]);
  return fd >= 0 && close(fd) == 0;
#endif
}

// Expects a call that failed and set an error string starting with prefix;
// annoy allocates the string with malloc.  error is a reference, read only
// after the call that sets it, whatever the argument evaluation order.
bool failed_with(bool ok, char*& error, const char* prefix) {
  const bool matches = !ok && error != NULL &&
                       std::strncmp(error, prefix, std::strlen(prefix)) == 0;
  std::free(error);
  error = NULL;
  return matches;
}

std::vector<float> vector_of(const float* data, int item) {
  return std::vector<float>(data + item * kDim, data + (item + 1) * kDim);
}

// The k nearest items to item, by get_distance over every item, ties by id.
template <typename Index>
void brute_force(const Index& index, int item, std::vector<int>& ids,
                 std::vector<float>& distances) {
  std::vector<std::pair<float, int> > all;
  for (int j = 0; j < index.get_n_items(); ++j) {
    all.push_back(std::make_pair(index.get_distance(item, j), j));
  }
  std::sort(all.begin(), all.end());
  ids.clear();
  distances.clear();
  for (int i = 0; i < kNeighbours; ++i) {
    ids.push_back(all[i].second);
    distances.push_back(all[i].first);
  }
}

// Every item's exhaustive neighbours match brute force, and a search by the
// item's vector matches the search by the item.
template <typename Index>
bool exact_everywhere(const Index& index, const float* data) {
  bool ok = true;
  for (int item = 0; item < kItems; ++item) {
    std::vector<int> ids, expected_ids;
    std::vector<float> distances, expected_distances;
    index.get_nns_by_item(item, kNeighbours, kSearchAll, &ids, &distances);
    brute_force(index, item, expected_ids, expected_distances);
    ok = ok && ids == expected_ids && distances == expected_distances &&
         ids[0] == item && distances[0] == 0.0f;

    std::vector<int> by_vector;
    std::vector<float> by_vector_distances;
    index.get_nns_by_vector(&data[item * kDim], kNeighbours, kSearchAll,
                            &by_vector, &by_vector_distances);
    ok = ok && by_vector == ids && by_vector_distances == distances;
  }
  return ok;
}

// Small vectors whose distances are exact in float.
void check_distances() {
  const float a[4] = {1.0f, 2.0f, 2.0f, 4.0f};
  const float origin[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  const float b[4] = {3.0f, -1.0f, 0.0f, 2.0f};
  const float x[4] = {1.0f, 0.0f, 0.0f, 0.0f};
  const float y[4] = {0.0f, 1.0f, 0.0f, 0.0f};
  const float x2[4] = {2.0f, 0.0f, 0.0f, 0.0f};

  Annoy::AnnoyIndex<int, float, Annoy::Euclidean, Annoy::Kiss32Random, Policy>
      euclidean(4);
  euclidean.add_item(0, a);
  euclidean.add_item(1, origin);
  euclidean.build(1);
  CHECK(euclidean.get_distance(0, 1) == 5.0f);

  Annoy::AnnoyIndex<int, float, Annoy::Manhattan, Annoy::Kiss32Random, Policy>
      manhattan(4);
  manhattan.add_item(0, a);
  manhattan.add_item(1, b);
  manhattan.build(1);
  CHECK(manhattan.get_distance(0, 1) == 9.0f);

  // sqrt(2 - 2 cos): 0 for parallel vectors, sqrt(2) for orthogonal ones.
  Annoy::AnnoyIndex<int, float, Annoy::Angular, Annoy::Kiss32Random, Policy>
      angular(4);
  angular.add_item(0, x);
  angular.add_item(1, y);
  angular.add_item(2, x2);
  angular.build(1);
  CHECK(angular.get_distance(0, 2) == 0.0f);
  CHECK(std::fabs(angular.get_distance(0, 1) - std::sqrt(2.0f)) < 1e-6f);

  // The dot product itself, largest first in searches.
  Annoy::AnnoyIndex<int, float, Annoy::DotProduct, Annoy::Kiss32Random, Policy>
      dot(4);
  dot.add_item(0, a);
  dot.add_item(1, b);
  dot.add_item(2, x);
  dot.build(1);
  CHECK(dot.get_distance(0, 1) == 9.0f);
  std::vector<int> best;
  dot.get_nns_by_vector(a, 3, -1, &best, NULL);
  CHECK(best.size() == 3 && best[0] == 0 && best[1] == 1 && best[2] == 2);

  // Bit vectors: the number of differing bits.
  typedef Annoy::AnnoyIndex<int, uint64_t, Annoy::Hamming,
                            Annoy::Kiss32Random, Policy>
      HammingIndex;
  HammingIndex hamming(2);
  const uint64_t p[2] = {0xF0F0u, 0x1u};
  const uint64_t q[2] = {0x0FF0u, 0x3u};
  hamming.add_item(0, p);
  hamming.add_item(1, q);
  hamming.build(1);
  CHECK(hamming.get_distance(0, 1) == 9u);
}

}  // namespace

int main() {
  check_distances();

  // Whole coordinates from 0 to 255, from a fixed linear congruential
  // sequence.  Every squared distance is then an exact integer below 2^24,
  // whose float square roots differ when the integers do, so the search's
  // order (squared distance, then id) and brute force's (distance, then id)
  // agree under any summation order.
  std::vector<float> data(kItems * kDim);
  unsigned long state = 2024;
  for (size_t i = 0; i < data.size(); ++i) {
    state = (state * 1103515245ul + 12345ul) & 0x7ffffffful;
    data[i] = static_cast<float>(state >> 23);
  }

  std::string saved, on_disk;
  if (!temp_path(saved) || !temp_path(on_disk)) {
    std::fprintf(stderr, "main.cc: cannot create temporary files\n");
    return 1;
  }

  {
    EuclideanIndex index(kDim);
    CHECK(index.get_f() == kDim);
    for (int i = 0; i < kItems; ++i) {
      CHECK(index.add_item(i, &data[i * kDim]));
    }
    char* error = NULL;
    CHECK(failed_with(index.save(saved.c_str(), false, &error), error,
                      "You can't save an index that hasn't been built"));
    CHECK(index.build(kTrees));
    CHECK(index.get_n_items() == kItems);
    CHECK(index.get_n_trees() == kTrees);
    error = NULL;
    CHECK(failed_with(index.build(kTrees, -1, &error), error,
                      "You can't build a built index"));

    std::vector<float> stored(kDim);
    index.get_item(42, &stored[0]);
    CHECK(stored == vector_of(&data[0], 42));
    CHECK(exact_everywhere(index, &data[0]));

    // save() writes the file and maps it back in place of the heap copy.
    CHECK(index.save(saved.c_str()));
    CHECK(index.get_n_items() == kItems && index.get_n_trees() == kTrees);
    CHECK(exact_everywhere(index, &data[0]));
    error = NULL;
    CHECK(failed_with(index.add_item(kItems, &data[0], &error), error,
                      "You can't add an item to a loaded index"));

    EuclideanIndex reader(kDim);
    CHECK(reader.load(saved.c_str()));
    CHECK(reader.get_n_items() == kItems && reader.get_n_trees() == kTrees);
    CHECK(exact_everywhere(reader, &data[0]));
    reader.get_item(42, &stored[0]);
    CHECK(stored == vector_of(&data[0], 42));
  }

  {
    // Built directly in a file, then loaded from it.
    EuclideanIndex builder(kDim);
    CHECK(builder.on_disk_build(on_disk.c_str()));
    for (int i = 0; i < kItems; ++i) {
      builder.add_item(i, &data[i * kDim]);
    }
    CHECK(builder.build(kTrees));
    builder.unload();
    CHECK(builder.load(on_disk.c_str()));
    CHECK(builder.get_n_items() == kItems && builder.get_n_trees() == kTrees);
    CHECK(exact_everywhere(builder, &data[0]));
  }

  // Every index is destroyed, so nothing maps the files any more.
  CHECK(std::remove(saved.c_str()) == 0);
  CHECK(std::remove(on_disk.c_str()) == 0);
  EuclideanIndex missing(kDim);
  char* error = NULL;
  CHECK(failed_with(missing.load(saved.c_str(), false, &error), error,
                    "Unable to open"));

  if (failures != 0) {
    return 1;
  }
  std::printf("annoy ok\n");
  return 0;
}
