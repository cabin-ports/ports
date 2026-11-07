/* meshoptimizer from C: the C API of the C++ library, first with its
   default allocator (::operator new, so the link needs the C++ runtime)
   and then with a counting one.  Decodes upstream's reference index and
   vertex encodings and an octahedral normal filter, round-trips both
   codecs, deduplicates, reorders and simplifies a mesh, splits it into
   meshlets, and converts half floats. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <meshoptimizer.h>

static int failures = 0;

static void check(int ok, int line, const char *what) {
  if (!ok) {
    fprintf(stderr, "main.c:%d: check failed: %s\n", line, what);
    ++failures;
  }
}

#define CHECK(cond) check((cond) != 0, __LINE__, #cond)

/* Upstream's reference encodings and what they decode to
   (demo/tests.cpp:14-65). */
struct PV {
  unsigned short px, py, pz;
  unsigned char nu, nv;
  unsigned short tx, ty;
};

static const unsigned int kIndexBufferTricky[] = {0, 1, 2, 2, 1, 3, 0, 1,
                                                  2, 2, 1, 5, 2, 1, 4};

static const unsigned char kIndexDataV1[] = {
    0xe1, 0xf0, 0x10, 0xfe, 0x1f, 0x3d, 0x00, 0x0a, 0x00, 0x76, 0x87, 0x56,
    0x67, 0x78, 0xa9, 0x86, 0x65, 0x89, 0x68, 0x98, 0x01, 0x69, 0x00, 0x00};

static const unsigned int kIndexSequence[] = {0, 1, 51, 2, 49, 1000};

static const unsigned char kIndexSequenceV1[] = {
    0xd1, 0x00, 0x04, 0xcd, 0x01, 0x04, 0x07, 0x98, 0x1f, 0x00, 0x00, 0x00,
    0x00};

static const struct PV kVertexBuffer[] = {
    {0, 0, 0, 0, 0, 0, 0},
    {300, 0, 0, 0, 0, 500, 0},
    {0, 300, 0, 0, 0, 0, 500},
    {300, 300, 0, 0, 0, 500, 500},
};

static const unsigned char kVertexDataV1[] = {
    0xa1, 0xee, 0xaa, 0xee, 0x00, 0x4b, 0x4b, 0x4b, 0x00, 0x00, 0x4b,
    0x00, 0x00, 0x7d, 0x7d, 0x7d, 0x00, 0x00, 0x7d, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x62, 0x00, 0x62};

/* A flat grid of GRID x GRID quads, two triangles each. */
#define GRID 16
#define GRID_VERTICES ((GRID + 1) * (GRID + 1))
#define GRID_INDICES (GRID * GRID * 6)

struct Vertex {
  float x, y, z;
  unsigned int rgba;
};

static struct Vertex grid_vertices[GRID_VERTICES];
static unsigned int grid_indices[GRID_INDICES];

static void make_grid(void) {
  unsigned int x, y, i = 0;
  for (y = 0; y <= GRID; ++y) {
    for (x = 0; x <= GRID; ++x) {
      struct Vertex *v = &grid_vertices[y * (GRID + 1) + x];
      v->x = (float)x;
      v->y = (float)y;
      v->z = 0.0f;
      v->rgba = 0xff000000u | (y << 8) | x;
    }
  }
  for (y = 0; y < GRID; ++y) {
    for (x = 0; x < GRID; ++x) {
      unsigned int a = y * (GRID + 1) + x, b = a + 1, c = a + GRID + 1,
                   d = c + 1;
      grid_indices[i++] = a;
      grid_indices[i++] = b;
      grid_indices[i++] = c;
      grid_indices[i++] = c;
      grid_indices[i++] = b;
      grid_indices[i++] = d;
    }
  }
}

/* Rewrites each triangle in a canonical form: rotated to start at its
   smallest index, which keeps the winding, or else fully sorted. */
static void canonicalize(unsigned int *indices, size_t count,
                         int keep_winding) {
  size_t t;
  for (t = 0; t < count; t += 3) {
    unsigned int *v = &indices[t], tmp;
    if (keep_winding) {
      while (v[0] > v[1] || v[0] > v[2]) {
        tmp = v[0];
        v[0] = v[1];
        v[1] = v[2];
        v[2] = tmp;
      }
    } else {
      if (v[0] > v[1]) { tmp = v[0]; v[0] = v[1]; v[1] = tmp; }
      if (v[1] > v[2]) { tmp = v[1]; v[1] = v[2]; v[2] = tmp; }
      if (v[0] > v[1]) { tmp = v[0]; v[0] = v[1]; v[1] = tmp; }
    }
  }
}

static int compare_triangles(const void *a, const void *b) {
  const unsigned int *x = (const unsigned int *)a;
  const unsigned int *y = (const unsigned int *)b;
  int i;
  for (i = 0; i < 3; ++i) {
    if (x[i] != y[i]) {
      return x[i] < y[i] ? -1 : 1;
    }
  }
  return 0;
}

/* Whether a and b hold the same triangles in any order (with the same
   winding if keep_winding). */
static int same_triangle_set(const unsigned int *a, const unsigned int *b,
                             size_t count, int keep_winding) {
  static unsigned int x[GRID_INDICES], y[GRID_INDICES];
  memcpy(x, a, count * sizeof(unsigned int));
  memcpy(y, b, count * sizeof(unsigned int));
  canonicalize(x, count, keep_winding);
  canonicalize(y, count, keep_winding);
  qsort(x, count / 3, 3 * sizeof(unsigned int), compare_triangles);
  qsort(y, count / 3, 3 * sizeof(unsigned int), compare_triangles);
  return memcmp(x, y, count * sizeof(unsigned int)) == 0;
}

/* Whether b holds a's triangles in a's order, each possibly rotated
   (README.md:447: the index codec may rotate triangles, keeping their
   winding). */
static int same_triangles(const unsigned int *a, const unsigned int *b,
                          size_t count) {
  size_t t;
  for (t = 0; t < count; t += 3) {
    int r, match = 0;
    for (r = 0; r < 3; ++r) {
      match |= b[t] == a[t + r] && b[t + 1] == a[t + (r + 1) % 3] &&
               b[t + 2] == a[t + (r + 2) % 3];
    }
    if (!match) {
      return 0;
    }
  }
  return 1;
}

static void check_reference_decoding(void) {
  unsigned int indices[15];
  unsigned short indices16[15];
  unsigned int sequence[6];
  struct PV vertices[4];
  size_t i;

  CHECK(sizeof(struct PV) == 12);
  CHECK(meshopt_decodeIndexVersion(kIndexDataV1, sizeof(kIndexDataV1)) == 1);
  CHECK(meshopt_decodeIndexBuffer(indices, 15, 4, kIndexDataV1,
                                  sizeof(kIndexDataV1)) == 0);
  CHECK(memcmp(indices, kIndexBufferTricky, sizeof(indices)) == 0);
  CHECK(meshopt_decodeIndexBuffer(indices16, 15, 2, kIndexDataV1,
                                  sizeof(kIndexDataV1)) == 0);
  for (i = 0; i < 15; ++i) {
    CHECK(indices16[i] == kIndexBufferTricky[i]);
  }
  /* A truncated buffer is rejected, not overrun. */
  CHECK(meshopt_decodeIndexBuffer(indices, 15, 4, kIndexDataV1,
                                  sizeof(kIndexDataV1) - 1) < 0);

  CHECK(meshopt_decodeIndexSequence(sequence, 6, 4, kIndexSequenceV1,
                                    sizeof(kIndexSequenceV1)) == 0);
  CHECK(memcmp(sequence, kIndexSequence, sizeof(sequence)) == 0);

  CHECK(meshopt_decodeVertexVersion(kVertexDataV1, sizeof(kVertexDataV1)) ==
        1);
  CHECK(meshopt_decodeVertexBuffer(vertices, 4, sizeof(struct PV),
                                   kVertexDataV1, sizeof(kVertexDataV1)) == 0);
  CHECK(memcmp(vertices, kVertexBuffer, sizeof(vertices)) == 0);
}

static void check_octahedral_filter(void) {
  /* Upstream's decodeFilterOct8() vectors: four 8-bit octahedral normals
     decoded in place, a full group of four and a three-element tail. */
  static const unsigned char data[16] = {0,  1,   127, 0, 0,   187, 127, 1,
                                         255, 1,  127, 0, 14,  130, 127, 1};
  static const unsigned char expected[16] = {0,   1,   127, 0, 0, 159, 82,  1,
                                             255, 1,   127, 0, 1, 130, 241, 1};
  unsigned char full[16], tail[12];

  memcpy(full, data, sizeof(full));
  meshopt_decodeFilterOct(full, 4, 4);
  CHECK(memcmp(full, expected, sizeof(full)) == 0);
  memcpy(tail, data, sizeof(tail));
  meshopt_decodeFilterOct(tail, 3, 4);
  CHECK(memcmp(tail, expected, sizeof(tail)) == 0);
}

static void check_round_trips(void) {
  static unsigned char buffer[1 << 16];
  static unsigned int indices[GRID_INDICES];
  static struct Vertex vertices[GRID_VERTICES];
  size_t size;

  CHECK(meshopt_encodeIndexBufferBound(GRID_INDICES, GRID_VERTICES) <=
        sizeof(buffer));
  size = meshopt_encodeIndexBuffer(buffer, sizeof(buffer), grid_indices,
                                   GRID_INDICES);
  CHECK(size > 0 && size < sizeof(grid_indices));
  CHECK(meshopt_decodeIndexBuffer(indices, GRID_INDICES, 4, buffer, size) ==
        0);
  CHECK(same_triangles(grid_indices, indices, GRID_INDICES));

  CHECK(meshopt_encodeVertexBufferBound(GRID_VERTICES, sizeof(struct Vertex)) <=
        sizeof(buffer));
  size = meshopt_encodeVertexBuffer(buffer, sizeof(buffer), grid_vertices,
                                    GRID_VERTICES, sizeof(struct Vertex));
  CHECK(size > 0 && size < sizeof(grid_vertices));
  CHECK(meshopt_decodeVertexBuffer(vertices, GRID_VERTICES,
                                   sizeof(struct Vertex), buffer, size) == 0);
  CHECK(memcmp(vertices, grid_vertices, sizeof(vertices)) == 0);
}

static void check_remap(void) {
  /* The grid as an unindexed triangle list: every corner a copy. */
  static struct Vertex soup[GRID_INDICES];
  static struct Vertex unique[GRID_INDICES];
  static unsigned int remap[GRID_INDICES];
  static unsigned int indices[GRID_INDICES];
  size_t i, count;
  int ok = 1;

  for (i = 0; i < GRID_INDICES; ++i) {
    soup[i] = grid_vertices[grid_indices[i]];
  }
  count = meshopt_generateVertexRemap(remap, NULL, GRID_INDICES, soup,
                                      GRID_INDICES, sizeof(struct Vertex));
  CHECK(count == GRID_VERTICES);
  meshopt_remapVertexBuffer(unique, soup, GRID_INDICES, sizeof(struct Vertex),
                            remap);
  meshopt_remapIndexBuffer(indices, NULL, GRID_INDICES, remap);
  for (i = 0; i < GRID_INDICES; ++i) {
    ok &= indices[i] < count &&
          memcmp(&unique[indices[i]], &soup[i], sizeof(struct Vertex)) == 0;
  }
  CHECK(ok);
}

static void check_reordering(void) {
  static unsigned int scrambled[GRID_INDICES];
  static unsigned int optimized[GRID_INDICES];
  static unsigned int fetched[GRID_INDICES];
  static struct Vertex vertices[GRID_VERTICES];
  struct meshopt_VertexCacheStatistics before, after;
  size_t t, i, seen = 0;
  int ok = 1;

  /* Visit the triangles in a scattered order: 97 is coprime to their
     count, so triangle t takes triangle (t * 97) mod count's place. */
  for (t = 0; t < GRID_INDICES / 3; ++t) {
    memcpy(&scrambled[t * 3], &grid_indices[(t * 97) % (GRID_INDICES / 3) * 3],
           3 * sizeof(unsigned int));
  }
  before = meshopt_analyzeVertexCache(scrambled, GRID_INDICES, GRID_VERTICES,
                                      16, 0, 0);
  meshopt_optimizeVertexCache(optimized, scrambled, GRID_INDICES,
                              GRID_VERTICES);
  after = meshopt_analyzeVertexCache(optimized, GRID_INDICES, GRID_VERTICES,
                                     16, 0, 0);
  /* Reordering keeps every triangle and its winding. */
  CHECK(same_triangle_set(optimized, grid_indices, GRID_INDICES, 1));
  /* ACMR: vertices transformed per triangle; a grid's optimum is about
     0.56 (every vertex once), a scattered order transforms most corners. */
  CHECK(after.acmr < 1.0f && after.acmr < before.acmr);

  /* The fetch optimizer numbers vertices in order of first use. */
  memcpy(fetched, optimized, sizeof(fetched));
  CHECK(meshopt_optimizeVertexFetch(vertices, fetched, GRID_INDICES,
                                    grid_vertices, GRID_VERTICES,
                                    sizeof(struct Vertex)) == GRID_VERTICES);
  for (i = 0; i < GRID_INDICES; ++i) {
    if (fetched[i] == seen) {
      ++seen;
    }
    ok &= fetched[i] < seen &&
          memcmp(&vertices[fetched[i]], &grid_vertices[optimized[i]],
                 sizeof(struct Vertex)) == 0;
  }
  CHECK(ok && seen == GRID_VERTICES);
}

static void check_simplify(void) {
  /* Upstream's simplify() test (demo/tests.cpp): a fan of four triangles
     over six coplanar points collapses losslessly to its outline. */
  unsigned int ib[] = {0, 2, 1, 1, 2, 3, 3, 2, 4, 2, 5, 4};
  static const float vb[] = {0, 4, 0, 0, 1, 0, 2, 2, 0,
                             0, 0, 0, 1, 0, 0, 4, 0, 0};
  static const unsigned int expected[] = {0, 5, 3};
  float error = 1.0f;

  CHECK(meshopt_simplify(ib, ib, 12, vb, 6, 12, 3, 1e-2f, 0, &error) == 3);
  CHECK(error < 1e-4f);
  CHECK(memcmp(ib, expected, sizeof(expected)) == 0);
}

static void check_meshlets(void) {
  static struct meshopt_Meshlet meshlets[GRID_INDICES / 3];
  static unsigned int meshlet_vertices[GRID_INDICES];
  static unsigned char meshlet_triangles[GRID_INDICES];
  static unsigned int triangles[GRID_INDICES];
  size_t count, i, j, used = 0, total = 0;
  int ok = 1;

  CHECK(meshopt_buildMeshletsBound(GRID_INDICES, 64, 124) <=
        sizeof(meshlets) / sizeof(meshlets[0]));
  count = meshopt_buildMeshlets(meshlets, meshlet_vertices, meshlet_triangles,
                                grid_indices, GRID_INDICES,
                                &grid_vertices[0].x, GRID_VERTICES,
                                sizeof(struct Vertex), 64, 124, 0.0f);
  CHECK(count > 1);
  for (i = 0; i < count; ++i) {
    const struct meshopt_Meshlet *m = &meshlets[i];
    ok &= m->vertex_count <= 64 && m->triangle_count <= 124;
    total += m->triangle_count * 3;
    for (j = 0; j < m->triangle_count * 3 && used < GRID_INDICES; ++j) {
      unsigned char local = meshlet_triangles[m->triangle_offset + j];
      ok &= local < m->vertex_count;
      triangles[used++] = meshlet_vertices[m->vertex_offset + local];
    }
  }
  /* The meshlets partition the mesh; upstream's tests leave the winding
     inside a meshlet open (demo/tests.cpp, meshletsDense). */
  CHECK(ok && total == GRID_INDICES);
  CHECK(same_triangle_set(triangles, grid_indices, GRID_INDICES, 0));
}

static void check_half(void) {
  /* Values from upstream's quantizeHalf() and dequantizeHalf() tests. */
  CHECK(meshopt_quantizeHalf(1.2345f) == 0x3cf0);
  CHECK(meshopt_quantizeHalf(65535.f) == 0x7c00);
  CHECK(meshopt_quantizeHalf(-0.125f) == 0xb000);
  CHECK(meshopt_quantizeHalf(1e-4f) == 0x068e);
  CHECK(meshopt_quantizeHalf(1e-5f) == 0x0000);
  CHECK(meshopt_dequantizeHalf(0x3cf0) == 1.234375f);
  CHECK(meshopt_dequantizeHalf(0x7bef) == 64992.f);
}

static int alloc_count = 0;
static int free_count = 0;

static void *MESHOPTIMIZER_ALLOC_CALLCONV count_alloc(size_t size) {
  ++alloc_count;
  return malloc(size);
}

static void MESHOPTIMIZER_ALLOC_CALLCONV count_free(void *ptr) {
  ++free_count;
  free(ptr);
}

static void check_allocator(void) {
  /* Counts from upstream's customAllocator() test. */
  float vb[] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
  unsigned int ib[] = {0, 1, 2};

  meshopt_setAllocator(count_alloc, count_free);
  meshopt_computeClusterBounds(ib, 3, vb, 3, 12);
  CHECK(alloc_count == 0 && free_count == 0);
  /* A remap table and a copy of the vertices for the in-place remap. */
  meshopt_optimizeVertexFetch(vb, ib, 3, vb, 3, 12);
  CHECK(alloc_count == 2 && free_count == 2);
}

int main(void) {
  make_grid();
  check_reference_decoding();
  check_octahedral_filter();
  check_round_trips();
  check_remap();
  check_reordering();
  check_simplify();
  check_meshlets();
  check_half();
  check_allocator();
  if (failures != 0) {
    return 1;
  }
  printf("meshoptimizer %d: C API OK\n", MESHOPTIMIZER_VERSION);
  return 0;
}
