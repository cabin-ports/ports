/* Checks FLANN's C API: exact k-nearest-neighbour and radius searches over
   a single kd-tree on five 2-D points.  Distances are squared Euclidean. */
#include <math.h>
#include <stdio.h>

#include <flann/flann.h>

static int failures;

static void expect(int ok, const char* what) {
  if (!ok) {
    fprintf(stderr, "failed: %s\n", what);
    ++failures;
  }
}

static int near(float a, float b) { return fabsf(a - b) < 1e-4f; }

int main(void) {
  float dataset[] = {0, 0, 1, 0, 0, 1, 5, 5, 6, 5};
  float queries[] = {0.9f, 0.1f, 5.8f, 5.1f};
  int indices[4];
  float dists[4];
  struct FLANNParameters params = DEFAULT_FLANN_PARAMETERS;
  params.algorithm = FLANN_INDEX_KDTREE_SINGLE;
  params.log_level = FLANN_LOG_NONE;

  float speedup = 0;
  flann_index_t idx = flann_build_index(dataset, 5, 2, &speedup, &params);
  expect(idx != NULL, "flann_build_index");
  if (idx == NULL) return 1;

  int rc = flann_find_nearest_neighbors_index(idx, queries, 2, indices,
                                              dists, 2, &params);
  expect(rc == 0, "flann_find_nearest_neighbors_index");
  expect(indices[0] == 1 && indices[1] == 0 && indices[2] == 4 &&
             indices[3] == 3,
         "nearest neighbours");
  expect(near(dists[0], 0.02f) && near(dists[1], 0.82f) &&
             near(dists[2], 0.05f) && near(dists[3], 0.65f),
         "squared distances");

  /* Within a squared radius of 1.5 of the origin: points 0, 1 and 2. */
  float origin[] = {0, 0};
  int found[5];
  float found_dists[5];
  int seen = 0;
  int count = flann_radius_search(idx, origin, found, found_dists, 5, 1.5f,
                                  &params);
  for (int i = 0; i < count && i < 5; ++i) seen |= 1 << found[i];
  expect(count == 3 && seen == 7, "radius search");

  expect(flann_free_index(idx, &params) == 0, "flann_free_index");

  if (failures != 0) return 1;
  puts("flann C API OK");
  return 0;
}
