// meshoptimizer from C++: the templated wrappers over 16-bit index
// buffers, the inline quantizers, and meshopt_Allocator over a counting
// allocator.
#include <algorithm>
#include <cstdio>
#include <vector>

#include <meshoptimizer.h>

namespace {

int failures = 0;

void check(bool ok, int line, const char *what) {
  if (!ok) {
    std::fprintf(stderr, "main.cc:%d: check failed: %s\n", line, what);
    ++failures;
  }
}

#define CHECK(cond) check((cond), __LINE__, #cond)

int alloc_count = 0;
int free_count = 0;

void *MESHOPTIMIZER_ALLOC_CALLCONV count_alloc(size_t size) {
  ++alloc_count;
  return ::operator new(size);
}

void MESHOPTIMIZER_ALLOC_CALLCONV count_free(void *ptr) {
  ++free_count;
  ::operator delete(ptr);
}

// The triangles of an index buffer, each rotated to start at its smallest
// index (which keeps its winding), sorted.
std::vector<unsigned long long> triangle_set(
    const std::vector<unsigned short> &indices) {
  std::vector<unsigned long long> set;
  for (size_t t = 0; t < indices.size(); t += 3) {
    unsigned long long v[3] = {indices[t], indices[t + 1], indices[t + 2]};
    while (v[0] > v[1] || v[0] > v[2]) {
      unsigned long long first = v[0];
      v[0] = v[1];
      v[1] = v[2];
      v[2] = first;
    }
    set.push_back(v[0] << 32 | v[1] << 16 | v[2]);
  }
  std::sort(set.begin(), set.end());
  return set;
}

}  // namespace

int main() {
  // Upstream's simplify() test (demo/tests.cpp) with 16-bit indices: the
  // wrapper widens them, simplifies, and narrows the result.
  unsigned short ib[] = {0, 2, 1, 1, 2, 3, 3, 2, 4, 2, 5, 4};
  const float vb[] = {0, 4, 0, 0, 1, 0, 2, 2, 0, 0, 0, 0, 1, 0, 0, 4, 0, 0};
  float error = 1.0f;
  CHECK(meshopt_simplify(ib, ib, 12, vb, 6, 12, 3, 1e-2f, 0, &error) == 3);
  CHECK(error < 1e-4f);
  CHECK(ib[0] == 0 && ib[1] == 5 && ib[2] == 3);

  // An 8x8 quad grid as 16-bit indices: reorder for the vertex cache, then
  // encode and decode through the wrappers.
  const int n = 8;
  std::vector<unsigned short> grid;
  for (int y = 0; y < n; ++y) {
    for (int x = 0; x < n; ++x) {
      unsigned short a = static_cast<unsigned short>(y * (n + 1) + x);
      unsigned short b = static_cast<unsigned short>(a + 1);
      unsigned short c = static_cast<unsigned short>(a + n + 1);
      unsigned short d = static_cast<unsigned short>(c + 1);
      unsigned short quad[] = {a, b, c, c, b, d};
      grid.insert(grid.end(), quad, quad + 6);
    }
  }
  const size_t vertex_count = (n + 1) * (n + 1);
  std::vector<unsigned short> optimized(grid.size());
  meshopt_optimizeVertexCache(&optimized[0], &grid[0], grid.size(),
                              vertex_count);
  // Reordering keeps every triangle and its winding.
  CHECK(triangle_set(optimized) == triangle_set(grid));

  std::vector<unsigned char> encoded(
      meshopt_encodeIndexBufferBound(grid.size(), vertex_count));
  encoded.resize(meshopt_encodeIndexBuffer(encoded.data(), encoded.size(),
                                           optimized.data(), optimized.size()));
  CHECK(!encoded.empty());
  std::vector<unsigned short> decoded(optimized.size());
  CHECK(meshopt_decodeIndexBuffer(decoded.data(), decoded.size(),
                                  encoded.data(), encoded.size()) == 0);
  // The codec keeps the triangle order but may rotate each triangle,
  // keeping its winding.
  bool same_triangles = true;
  for (size_t t = 0; t < decoded.size(); t += 3) {
    bool match = false;
    for (size_t r = 0; r < 3; ++r) {
      match |= decoded[t] == optimized[t + r] &&
               decoded[t + 1] == optimized[t + (r + 1) % 3] &&
               decoded[t + 2] == optimized[t + (r + 2) % 3];
    }
    same_triangles &= match;
  }
  CHECK(same_triangles);

  // The quantizers round to nearest and clamp.
  CHECK(meshopt_quantizeUnorm(1.0f, 10) == 1023);
  CHECK(meshopt_quantizeUnorm(0.5f, 8) == 128);
  CHECK(meshopt_quantizeUnorm(-2.0f, 8) == 0);
  CHECK(meshopt_quantizeSnorm(-1.0f, 8) == -127);
  CHECK(meshopt_quantizeSnorm(2.0f, 8) == 127);
  CHECK(meshopt_quantizeSnorm(-0.5f, 16) == -16384);

  // meshopt_Allocator draws from the allocator meshopt_setAllocator
  // installs and frees what it handed out when it goes out of scope.
  meshopt_setAllocator(count_alloc, count_free);
  {
    meshopt_Allocator allocator;
    float *first = allocator.allocate<float>(1024);
    unsigned int *second = allocator.allocate<unsigned int>(16);
    first[1023] = 1.0f;
    second[15] = 2u;
    CHECK(alloc_count == 2 && free_count == 0);
  }
  CHECK(alloc_count == 2 && free_count == 2);

  if (failures != 0) {
    return 1;
  }
  std::printf("meshoptimizer %d: C++ API OK\n", MESHOPTIMIZER_VERSION);
  return 0;
}
