#include <tmd/TriangleMeshDistance.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <vector>

static int failures = 0;

static void check(bool ok, const char *what) {
  if (!ok) {
    std::cerr << "FAIL: " << what << '\n';
    ++failures;
  }
}

static bool near(double a, double b) { return std::abs(a - b) < 1e-12; }

static bool near(const tmd::Vec3d &p, double x, double y, double z) {
  return near(p[0], x) && near(p[1], y) && near(p[2], z);
}

int main() {
  using tmd::NearestEntity;

  // The unit cube [0,1]^3, two outward-wound triangles per face.
  const std::vector<std::array<double, 3>> vertices = {
      {{0, 0, 0}}, {{1, 0, 0}}, {{1, 1, 0}}, {{0, 1, 0}},
      {{0, 0, 1}}, {{1, 0, 1}}, {{1, 1, 1}}, {{0, 1, 1}}};
  const std::vector<std::array<int, 3>> triangles = {
      {{0, 2, 1}}, {{0, 3, 2}},   // z = 0
      {{4, 5, 6}}, {{4, 6, 7}},   // z = 1
      {{0, 1, 5}}, {{0, 5, 4}},   // y = 0
      {{3, 7, 6}}, {{3, 6, 2}},   // y = 1
      {{0, 4, 7}}, {{0, 7, 3}},   // x = 0
      {{1, 2, 6}}, {{1, 6, 5}}};  // x = 1
  tmd::TriangleMeshDistance cube(vertices, triangles);
  check(cube.is_mesh_manifold(), "the closed cube is manifold");

  // Inside, 0.2 above the bottom face, within triangle 1 {0, 3, 2}.
  tmd::Result r = cube.signed_distance({0.25, 0.5, 0.2});
  check(near(r.distance, -0.2) && r.nearest_entity == NearestEntity::F && r.triangle_id == 1 &&
            near(r.nearest_point, 0.25, 0.5, 0.0) && near(r.barycentric, 0.5, 0.25, 0.25),
        "inside: -0.2 to the bottom face");
  r = cube.unsigned_distance({0.25, 0.5, 0.2});
  check(near(r.distance, 0.2) && r.triangle_id == 1, "the unsigned distance of the inside point is +0.2");

  // Outside, 1 from the x = 1 face, within triangle 11 {1, 6, 5}.
  r = cube.signed_distance(tmd::Vec3d(2.0, 0.25, 0.5));
  check(near(r.distance, 1.0) && r.nearest_entity == NearestEntity::F && r.triangle_id == 11 &&
            near(r.nearest_point, 1.0, 0.25, 0.5),
        "outside: +1 to the x = 1 face");

  // On the top face, within triangle 2 {4, 5, 6}.
  r = cube.signed_distance({0.5, 0.25, 1.0});
  check(near(r.distance, 0.0) && r.nearest_entity == NearestEntity::F && r.triangle_id == 2 &&
            near(r.nearest_point, 0.5, 0.25, 1.0),
        "on a face: distance 0");

  // Outside the edge from vertex 5 to vertex 6, which is edge 1-2 (E12) of
  // both triangles that share it, 2 {4, 5, 6} and 11 {1, 6, 5}.
  r = cube.signed_distance({1.5, 0.5, 1.5});
  check(near(r.distance, std::sqrt(0.5)) && r.nearest_entity == NearestEntity::E12 &&
            (r.triangle_id == 2 || r.triangle_id == 11) && near(r.nearest_point, 1.0, 0.5, 1.0),
        "outside an edge: +sqrt(0.5) to its midpoint");

  // Outside the corner at vertex 6, a vertex of six triangles.
  r = cube.signed_distance({2.0, 2.0, 2.0});
  const int corner = r.nearest_entity == NearestEntity::V0   ? 0
                     : r.nearest_entity == NearestEntity::V1 ? 1
                     : r.nearest_entity == NearestEntity::V2 ? 2
                                                             : -1;
  check(near(r.distance, std::sqrt(3.0)) && corner >= 0 && r.triangle_id >= 0 && r.triangle_id < 12 &&
            triangles[static_cast<std::size_t>(r.triangle_id)][static_cast<std::size_t>(corner)] == 6 &&
            near(r.nearest_point, 1.0, 1.0, 1.0),
        "outside a corner: +sqrt(3) to vertex 6");

  // A tetrahedron, built into a default-constructed object.  The outward
  // normals at its edge from (0,1,0) to (0,0,1), (-1,0,0) and (1,1,1)/sqrt(3),
  // are more than 90 degrees apart, so each outside point below is on the
  // wrong side of one of the two triangles' planes; its sign must come from
  // the edge's pseudonormal.
  tmd::TriangleMeshDistance tetrahedron;
  tetrahedron.construct(
      std::vector<std::array<double, 3>>{{{0, 0, 0}}, {{1, 0, 0}}, {{0, 1, 0}}, {{0, 0, 1}}},
      std::vector<std::array<int, 3>>{{{0, 3, 2}}, {{0, 1, 3}}, {{0, 2, 1}}, {{1, 2, 3}}});
  check(tetrahedron.is_mesh_manifold(), "the tetrahedron is manifold");
  r = tetrahedron.signed_distance({0.05, 0.6, 0.6});
  check(near(r.distance, 0.15) && r.nearest_entity == NearestEntity::E12 && near(r.nearest_point, 0.0, 0.5, 0.5),
        "outside a sharp edge, behind the x = 0 plane: +0.15");
  r = tetrahedron.signed_distance({-0.3, 0.6, 0.6});
  check(near(r.distance, std::sqrt(0.11)) && r.nearest_entity == NearestEntity::E12 &&
            near(r.nearest_point, 0.0, 0.5, 0.5),
        "outside a sharp edge, behind the slanted face's plane: +sqrt(0.11)");
  r = tetrahedron.signed_distance({0.1, 0.1, 0.1});
  check(near(r.distance, -0.1), "inside the tetrahedron: -0.1");

  // The same cube without its top face, from flat arrays: not watertight,
  // and the unsigned distance still holds.
  double flat_vertices[24];
  int flat_triangles[30];
  for (std::size_t i = 0; i < 8; ++i) {
    for (std::size_t k = 0; k < 3; ++k) {
      flat_vertices[3 * i + k] = vertices[i][k];
    }
  }
  for (std::size_t i = 0, j = 0; i < 12; ++i) {
    if (i == 2 || i == 3) {
      continue;
    }
    for (std::size_t k = 0; k < 3; ++k) {
      flat_triangles[3 * j + k] = triangles[i][k];
    }
    ++j;
  }
  tmd::TriangleMeshDistance open_box(flat_vertices, 8, flat_triangles, 10);
  check(!open_box.is_mesh_manifold(), "the open box is not manifold");
  r = open_box.unsigned_distance({0.25, 0.5, -1.0});
  check(near(r.distance, 1.0) && near(r.nearest_point, 0.25, 0.5, 0.0), "open box: 1 below the bottom face");

  if (failures != 0) {
    std::cerr << failures << " check(s) failed\n";
    return 1;
  }
  std::cout << "trianglemeshdistance 1.1.0 C++ ok\n";
  return 0;
}
