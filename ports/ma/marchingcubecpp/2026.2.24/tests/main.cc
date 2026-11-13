#define MC_IMPLEM_ENABLE
#include <MC.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <utility>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

// For the mesh shape the later checks index through.
void require(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    std::exit(1);
  }
}

// field[(z * ny + y) * nx + x] = f(x, y, z), the layout marching_cube reads.
template <class F>
std::vector<MC::MC_FLOAT> Sample(unsigned nx, unsigned ny, unsigned nz, F f) {
  std::vector<MC::MC_FLOAT> field(nx * ny * nz);
  for (unsigned z = 0; z < nz; ++z) {
    for (unsigned y = 0; y < ny; ++y) {
      for (unsigned x = 0; x < nx; ++x) {
        field[(z * ny + y) * nx + x] = f(x, y, z);
      }
    }
  }
  return field;
}

bool IndicesValid(const MC::mcMesh& mesh) {
  if (mesh.indices.size() % 3 != 0) return false;
  for (MC::muint i : mesh.indices) {
    if (i >= mesh.vertices.size()) return false;
  }
  return mesh.normals.size() == mesh.vertices.size();
}

}  // namespace

int main() {
  // The plane z = 0.5 through a 3x3x3 grid: nine shared vertices on the
  // grid's x-y lattice and eight triangles, with exact coordinates.
  {
    std::vector<MC::MC_FLOAT> field =
        Sample(3, 3, 3, [](unsigned, unsigned, unsigned z) {
          return static_cast<MC::MC_FLOAT>(z) - 0.5f;
        });
    MC::mcMesh mesh;
    MC::marching_cube(field.data(), 3, 3, 3, mesh);
    require(mesh.vertices.size() == 9, "plane: nine shared vertices");
    require(mesh.indices.size() == 24, "plane: eight triangles");
    require(IndicesValid(mesh), "plane: indices and normals line up");
    std::set<std::pair<int, int>> lattice;
    bool on_plane = true;
    bool normals_axial = true;
    for (size_t i = 0; i < mesh.vertices.size(); ++i) {
      const MC::mcVec3f& v = mesh.vertices[i];
      const MC::mcVec3f& n = mesh.normals[i];
      const bool here = v.z == 0.5f && v.x == std::floor(v.x) &&
                        v.y == std::floor(v.y) && v.x >= 0 && v.x <= 2 &&
                        v.y >= 0 && v.y <= 2;
      on_plane = on_plane && here;
      if (here) {
        lattice.insert(std::make_pair(static_cast<int>(v.x),
                                      static_cast<int>(v.y)));
      }
      normals_axial = normals_axial && n.x == 0 && n.y == 0 &&
                      std::fabs(n.z) == 1.0f && n.z == mesh.normals[0].z;
    }
    check(on_plane, "plane: vertices at z = 0.5 on the lattice");
    check(lattice.size() == 9, "plane: every lattice point once");
    check(normals_axial, "plane: unit normals along z, all one way");
  }

  // A sphere of radius 10 in a 32^3 grid: a closed genus-0 surface.
  {
    const float c = 15.5f;
    const float r = 10.0f;
    std::vector<MC::MC_FLOAT> field =
        Sample(32, 32, 32, [&](unsigned x, unsigned y, unsigned z) {
          const float dx = x - c, dy = y - c, dz = z - c;
          return std::sqrt(dx * dx + dy * dy + dz * dz) - r;
        });
    MC::mcMesh mesh;
    MC::setDefaultArraySizes(64, 64, 256);
    MC::marching_cube(field.data(), 32, 32, 32, mesh);
    require(!mesh.vertices.empty(), "sphere: non-empty mesh");
    require(IndicesValid(mesh), "sphere: indices and normals line up");

    bool near_sphere = true;
    bool unit_normals = true;
    int outward = 0;
    for (size_t i = 0; i < mesh.vertices.size(); ++i) {
      const MC::mcVec3f& v = mesh.vertices[i];
      const MC::mcVec3f& n = mesh.normals[i];
      const float dx = v.x - c, dy = v.y - c, dz = v.z - c;
      const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
      near_sphere = near_sphere && std::fabs(dist - r) < 0.05f;
      unit_normals = unit_normals &&
                     std::fabs(n.x * n.x + n.y * n.y + n.z * n.z - 1) < 1e-4f;
      outward += (n.x * dx + n.y * dy + n.z * dz) > 0 ? 1 : -1;
    }
    check(near_sphere, "sphere: vertices within 0.05 of the radius");
    check(unit_normals, "sphere: unit normals");
    check(outward == static_cast<int>(mesh.vertices.size()) ||
              outward == -static_cast<int>(mesh.vertices.size()),
          "sphere: normals all face the same way");

    // Each edge borders exactly two triangles, and V - E + F == 2.
    std::map<std::pair<MC::muint, MC::muint>, int> edges;
    for (size_t t = 0; t < mesh.indices.size(); t += 3) {
      for (int k = 0; k < 3; ++k) {
        MC::muint a = mesh.indices[t + k];
        MC::muint b = mesh.indices[t + (k + 1) % 3];
        if (a > b) std::swap(a, b);
        ++edges[std::make_pair(a, b)];
      }
    }
    bool closed = true;
    for (const auto& e : edges) closed = closed && e.second == 2;
    const long euler = static_cast<long>(mesh.vertices.size()) -
                       static_cast<long>(edges.size()) +
                       static_cast<long>(mesh.indices.size() / 3);
    check(closed, "sphere: closed manifold");
    check(euler == 2, "sphere: Euler characteristic 2");
  }

  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("marchingcubecpp consumer test passed");
  return 0;
}
