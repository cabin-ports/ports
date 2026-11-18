#define ENABLE_VHACD_IMPLEMENTATION 1
#include <VHACD.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char* what) {
  if (!ok) {
    std::printf("FAIL: %s\n", what);
    ++failures;
  }
}

struct Mesh {
  std::vector<double> points;
  std::vector<uint32_t> triangles;
};

Mesh UnitCube() {
  Mesh m;
  m.points = {0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0,
              0, 0, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1};
  m.triangles = {0, 2, 1, 0, 3, 2, 4, 5, 6, 4, 6, 7, 0, 1, 5, 0, 5, 4,
                 1, 2, 6, 1, 6, 5, 2, 3, 7, 2, 7, 6, 3, 0, 4, 3, 4, 7};
  return m;
}

// The L-shaped polygon (0,0) (2,0) (2,1) (1,1) (1,2) (0,2) extruded from
// z = 0 to z = 1: volume 3, while its convex hull has volume 3.5.
Mesh LPrism() {
  const double xy[6][2] = {{0, 0}, {2, 0}, {2, 1}, {1, 1}, {1, 2}, {0, 2}};
  Mesh m;
  for (int z = 0; z < 2; ++z) {
    for (int i = 0; i < 6; ++i) {
      m.points.push_back(xy[i][0]);
      m.points.push_back(xy[i][1]);
      m.points.push_back(z);
    }
  }
  for (uint32_t i = 1; i < 5; ++i) {
    const uint32_t bottom[] = {0, i + 1, i};
    const uint32_t top[] = {6, 6 + i, 6 + i + 1};
    m.triangles.insert(m.triangles.end(), bottom, bottom + 3);
    m.triangles.insert(m.triangles.end(), top, top + 3);
  }
  for (uint32_t i = 0; i < 6; ++i) {
    const uint32_t j = (i + 1) % 6;
    const uint32_t side[] = {i, j, 6 + j, i, 6 + j, 6 + i};
    m.triangles.insert(m.triangles.end(), side, side + 6);
  }
  return m;
}

std::vector<VHACD::IVHACD::ConvexHull> Decompose(const Mesh& mesh) {
  VHACD::IVHACD* vhacd = VHACD::CreateVHACD();
  VHACD::IVHACD::Parameters params;
  params.m_resolution = 10000;  // The default is 400000.
  std::vector<VHACD::IVHACD::ConvexHull> hulls;
  if (vhacd->Compute(mesh.points.data(),
                     static_cast<uint32_t>(mesh.points.size() / 3),
                     mesh.triangles.data(),
                     static_cast<uint32_t>(mesh.triangles.size() / 3),
                     params)) {
    for (uint32_t i = 0; i < vhacd->GetNConvexHulls(); ++i) {
      VHACD::IVHACD::ConvexHull hull;
      if (vhacd->GetConvexHull(i, hull)) {
        hulls.push_back(hull);
      }
    }
  }
  vhacd->Release();
  return hulls;
}

bool AllVolumesPositive(const std::vector<VHACD::IVHACD::ConvexHull>& hulls) {
  for (const auto& hull : hulls) {
    if (!(hull.m_volume > 0)) {
      return false;
    }
  }
  return !hulls.empty();
}

double TotalVolume(const std::vector<VHACD::IVHACD::ConvexHull>& hulls) {
  double total = 0;
  for (const auto& hull : hulls) {
    total += hull.m_volume;
  }
  return total;
}

bool InsideBox(const std::vector<VHACD::IVHACD::ConvexHull>& hulls, double x,
               double y, double z) {
  const double eps = 1e-6;
  for (const auto& hull : hulls) {
    for (const auto& p : hull.m_points) {
      if (p.mX < -eps || p.mX > x + eps || p.mY < -eps || p.mY > y + eps ||
          p.mZ < -eps || p.mZ > z + eps) {
        return false;
      }
    }
  }
  return true;
}

void TestCube() {
  const auto hulls = Decompose(UnitCube());
  check(hulls.size() == 1, "a convex cube stays one hull");
  check(AllVolumesPositive(hulls), "cube hull volume is positive");
  check(std::fabs(TotalVolume(hulls) - 1.0) < 1e-9,
        "the cube hull's volume is 1");
  bool corners = hulls.size() == 1 && hulls[0].m_points.size() == 8;
  if (corners) {
    for (const auto& p : hulls[0].m_points) {
      const double c[] = {p.mX, p.mY, p.mZ};
      for (double v : c) {
        corners = corners && (std::fabs(v) < 1e-9 || std::fabs(v - 1) < 1e-9);
      }
    }
  }
  check(corners, "the cube hull's vertices are the cube's corners");
}

void TestLPrism() {
  const auto hulls = Decompose(LPrism());
  check(hulls.size() == 3, "the L prism splits into three hulls");
  check(AllVolumesPositive(hulls), "every L prism hull volume is positive");
  // Measured 3.034 on arm64 at this resolution; one hull around the L
  // would be 3.5.
  check(std::fabs(TotalVolume(hulls) - 3.0) < 0.1,
        "the hulls' volume approximates the L prism");
  check(InsideBox(hulls, 2, 2, 1), "hull vertices stay on the source mesh's box");
}

}  // namespace

int main() {
  TestCube();
  TestLPrism();
  if (failures != 0) {
    return 1;
  }
  std::printf("v-hacd consumer test passed\n");
  return 0;
}
