// Consumer test for cabin-ports/nanort: build a BVH over a triangle mesh with
// the built-in SAH predicate, then find closest hits (primitive, distance and
// barycentric coordinates), misses and ray-extent cut-offs with Traverse, and
// list primitives along a ray with ListNodeIntersections, whose result type is
// the StackVector from Chromium's stack_container.h.  Written in C++03, the
// standard upstream targets by default.
#include "nanort.h"

#include <cmath>
#include <cstdio>

static int failures = 0;

static void check(bool pass, const char *what) {
  if (!pass) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

static bool near(float a, float b) { return std::fabs(a - b) < 1e-6f; }

// Four unit squares in the z = 0 plane at x = 0, 2, 4 and 6, two triangles
// each (primitives 0-7), and one triangle at z = -1 under the first square
// (primitive 8).
static float vertices[3 * 19];
static unsigned int faces[3 * 9];

static void make_mesh() {
  for (unsigned int s = 0; s < 4; ++s) {
    const float x = 2.f * static_cast<float>(s);
    const float corners[4][2] = {{x, 0.f}, {x + 1.f, 0.f}, {x + 1.f, 1.f}, {x, 1.f}};
    for (unsigned int c = 0; c < 4; ++c) {
      vertices[3 * (4 * s + c) + 0] = corners[c][0];
      vertices[3 * (4 * s + c) + 1] = corners[c][1];
      vertices[3 * (4 * s + c) + 2] = 0.f;
    }
    const unsigned int base = 4 * s;
    const unsigned int tris[6] = {base, base + 1, base + 2, base, base + 2, base + 3};
    for (unsigned int i = 0; i < 6; ++i) faces[6 * s + i] = tris[i];
  }
  const float under[9] = {0.f, 0.f, -1.f, 1.f, 0.f, -1.f, 0.f, 1.f, -1.f};
  for (unsigned int i = 0; i < 9; ++i) vertices[3 * 16 + i] = under[i];
  faces[24] = 16;
  faces[25] = 17;
  faces[26] = 18;
}

static nanort::Ray<float> down_ray(float x, float y, float z) {
  nanort::Ray<float> ray;
  ray.org[0] = x;
  ray.org[1] = y;
  ray.org[2] = z;
  ray.dir[0] = 0.f;
  ray.dir[1] = 0.f;
  ray.dir[2] = -1.f;
  ray.min_t = 0.f;
  ray.max_t = 1e30f;
  return ray;
}

// ListNodeIntersections asks for an intersector that reports a [t_min, t_max]
// span per primitive.  This one reports the span along a -z ray through each
// triangle's bounding box, as a two-level traversal would for instance boxes.
class BoxIntersector {
 public:
  explicit BoxIntersector(const nanort::TriangleMesh<float> &mesh) : mesh_(mesh), x_(0.f), y_(0.f), z_(0.f) {}

  void PrepareTraversal(const nanort::Ray<float> &ray) const {
    x_ = ray.org[0];
    y_ = ray.org[1];
    z_ = ray.org[2];
  }

  bool Intersect(float *t_min, float *t_max, unsigned int prim) const {
    nanort::real3<float> bmin, bmax;
    mesh_.BoundingBox(&bmin, &bmax, prim);
    if (x_ < bmin[0] || x_ > bmax[0] || y_ < bmin[1] || y_ > bmax[1]) return false;
    *t_min = z_ - bmax[2];
    *t_max = z_ - bmin[2];
    return true;
  }

 private:
  const nanort::TriangleMesh<float> &mesh_;
  mutable float x_, y_, z_;
};

int main() {
  make_mesh();
  nanort::TriangleMesh<float> mesh(vertices, faces, sizeof(float) * 3);
  nanort::TriangleSAHPred<float> pred(vertices, faces, sizeof(float) * 3);
  nanort::BVHAccel<float> accel;
  check(accel.Build(9, mesh, pred), "Build succeeds");
  check(accel.GetStatistics().num_branch_nodes >= 1, "nine primitives need more than one leaf");

  float bmin[3], bmax[3];
  accel.BoundingBox(bmin, bmax);
  check(bmin[0] == 0.f && bmin[1] == 0.f && bmin[2] == -1.f && bmax[0] == 7.f && bmax[1] == 1.f && bmax[2] == 0.f,
        "the BVH bounds the mesh");

  nanort::TriangleIntersector<float> isector(vertices, faces, sizeof(float) * 3);
  nanort::TriangleIntersection<float> isect;

  // (4.75, 0.25) lies in primitive 4, (4, 0)-(5, 0)-(5, 1): p = (1 - u - v) p0
  // + u p1 + v p2 gives u = 0.5 and v = 0.25.
  check(accel.Traverse(down_ray(4.75f, 0.25f, 1.f), isector, &isect) && isect.prim_id == 4 && near(isect.t, 1.f) &&
            near(isect.u, 0.5f) && near(isect.v, 0.25f),
        "closest hit: primitive, distance and barycentrics");

  check(accel.Traverse(down_ray(6.25f, 0.75f, 1.f), isector, &isect) && isect.prim_id == 7, "the second triangle");

  // Over the first square the plane at z = 0 hides the triangle at z = -1.
  check(accel.Traverse(down_ray(0.75f, 0.125f, 1.f), isector, &isect) && isect.prim_id == 0 && near(isect.t, 1.f),
        "the nearest of two hits");
  check(accel.Traverse(down_ray(0.25f, 0.5f, -0.5f), isector, &isect) && isect.prim_id == 8 && near(isect.t, 0.5f),
        "a ray below the plane");

  check(!accel.Traverse(down_ray(1.5f, 0.5f, 1.f), isector, &isect), "a ray through the gap misses");
  nanort::Ray<float> short_ray = down_ray(4.75f, 0.25f, 1.f);
  short_ray.max_t = 0.5f;
  check(!accel.Traverse(short_ray, isector, &isect), "max_t cuts the ray short");

  // Primitives 0 and 1 (box z = 0, t = 1) and 8 (z = -1, t = 2), nearest first.
  BoxIntersector boxes(mesh);
  nanort::StackVector<nanort::NodeHit<float>, 128> hits;
  check(accel.ListNodeIntersections(down_ray(0.75f, 0.25f, 1.f), 16, boxes, &hits), "ListNodeIntersections");
  check(hits->size() == 3, "three primitives along the ray");
  if (hits->size() == 3) {
    check((hits[0].node_id == 0 && hits[1].node_id == 1) || (hits[0].node_id == 1 && hits[1].node_id == 0),
          "the two plane triangles first");
    check(near(hits[0].t_min, 1.f) && near(hits[1].t_min, 1.f), "their distance");
    check(hits[2].node_id == 8 && near(hits[2].t_min, 2.f), "the lower triangle last");
  }
  check(!accel.ListNodeIntersections(down_ray(1.5f, 0.5f, 1.f), 16, boxes, &hits) && hits->empty(),
        "an empty list for a miss");

  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("nanort 2024.11.10 C++ ok");
  return 0;
}
