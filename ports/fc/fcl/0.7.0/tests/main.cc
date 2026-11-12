// Checks collision and distance queries between primitives (analytic and
// through libccd's GJK and MPR), a triangle mesh, an OctoMap octree and a
// broadphase manager, against hand-computed geometry.
#include <fcl/fcl.h>
#include <octomap/octomap.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

static int failures = 0;

static void check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

static void check_near(double actual, double expected, double tolerance,
                       const char* what) {
  if (!(std::fabs(actual - expected) <= tolerance)) {
    std::fprintf(stderr, "FAIL: %s: %.9g, expected %.9g\n", what, actual,
                 expected);
    ++failures;
  }
}

static fcl::Transform3d at(double x, double y, double z) {
  fcl::Transform3d tf = fcl::Transform3d::Identity();
  tf.translation() = fcl::Vector3d(x, y, z);
  return tf;
}

static bool collide(const fcl::CollisionObjectd& a,
                    const fcl::CollisionObjectd& b,
                    fcl::CollisionResultd* out = nullptr) {
  fcl::CollisionRequestd request;
  request.enable_contact = true;
  fcl::CollisionResultd result;
  fcl::collide(&a, &b, request, result);
  if (out != nullptr) {
    *out = result;
  }
  return result.isCollision();
}

static double distance(const fcl::CollisionObjectd& a,
                       const fcl::CollisionObjectd& b) {
  fcl::DistanceRequestd request;
  fcl::DistanceResultd result;
  fcl::distance(&a, &b, request, result);
  return result.min_distance;
}

// A unit cube centred at the origin as 8 vertices and 12 triangles.
static std::shared_ptr<fcl::BVHModel<fcl::OBBRSSd>> cube_mesh() {
  std::vector<fcl::Vector3d> v;
  for (int i = 0; i < 8; ++i) {
    v.emplace_back((i & 1) ? 0.5 : -0.5, (i & 2) ? 0.5 : -0.5,
                   (i & 4) ? 0.5 : -0.5);
  }
  const int faces[12][3] = {{0, 1, 3}, {0, 3, 2}, {4, 6, 7}, {4, 7, 5},
                            {0, 4, 5}, {0, 5, 1}, {2, 3, 7}, {2, 7, 6},
                            {0, 2, 6}, {0, 6, 4}, {1, 5, 7}, {1, 7, 3}};
  std::vector<fcl::Triangle> t;
  for (const auto& f : faces) {
    t.emplace_back(f[0], f[1], f[2]);
  }
  auto model = std::make_shared<fcl::BVHModel<fcl::OBBRSSd>>();
  model->beginModel();
  model->addSubModel(v, t);
  model->endModel();
  return model;
}

int main() {
  check(std::strcmp(FCL_VERSION, "0.7.0") == 0, "FCL_VERSION");
  check(FCL_HAVE_OCTOMAP == 1, "FCL_HAVE_OCTOMAP");

  auto sphere = std::make_shared<fcl::Sphered>(1.0);
  auto small_sphere = std::make_shared<fcl::Sphered>(0.5);
  auto box = std::make_shared<fcl::Boxd>(1.0, 1.0, 1.0);
  auto cylinder = std::make_shared<fcl::Cylinderd>(0.5, 2.0);

  // Two unit spheres: analytic distance and penetration.
  {
    fcl::CollisionObjectd a(sphere, at(0, 0, 0));
    fcl::CollisionObjectd apart(sphere, at(3, 0, 0));
    fcl::CollisionObjectd overlapping(sphere, at(1.5, 0, 0));
    check(!collide(a, apart), "separated spheres do not collide");
    check_near(distance(a, apart), 1.0, 1e-9, "sphere-sphere distance");
    fcl::CollisionResultd result;
    check(collide(a, overlapping, &result), "overlapping spheres collide");
    check(result.numContacts() == 1, "one sphere-sphere contact");
    if (result.numContacts() == 1) {
      check_near(result.getContact(0).penetration_depth, 0.5, 1e-9,
                 "sphere-sphere penetration");
    }
  }

  // A cylinder (axis z) against a box goes through libccd, which the
  // catalog builds in single precision.
  {
    fcl::CollisionObjectd c(cylinder, at(0, 0, 0));
    fcl::CollisionObjectd apart(box, at(2, 0, 0));
    fcl::CollisionObjectd overlapping(box, at(0.8, 0, 0));
    check(!collide(c, apart), "separated cylinder and box do not collide");
    check_near(distance(c, apart), 1.0, 1e-3, "libccd GJK distance");
    fcl::CollisionResultd result;
    check(collide(c, overlapping, &result), "cylinder and box collide");
    if (result.numContacts() == 1) {
      check_near(result.getContact(0).penetration_depth, 0.2, 1e-2,
                 "libccd MPR penetration");
    } else {
      check(false, "one cylinder-box contact");
    }
  }

  // A triangle-mesh cube against a sphere of radius 0.5.
  {
    fcl::CollisionObjectd mesh(cube_mesh(), at(0, 0, 0));
    fcl::CollisionObjectd touching(small_sphere, at(0.9, 0, 0));
    fcl::CollisionObjectd apart(small_sphere, at(1.2, 0, 0));
    check(collide(mesh, touching), "mesh and sphere collide");
    check(!collide(mesh, apart), "mesh and sphere apart");
    check_near(distance(mesh, apart), 0.2, 1e-6, "mesh-sphere distance");
  }

  // An OctoMap with one occupied voxel near (1, 0, 0).
  {
    auto map = std::make_shared<octomap::OcTree>(0.1);
    map->updateNode(octomap::point3d(1.0f, 0.0f, 0.0f), true);
    auto tree = std::make_shared<fcl::OcTreed>(map);
    fcl::CollisionObjectd octree(tree, at(0, 0, 0));
    auto probe = std::make_shared<fcl::Boxd>(0.3, 0.3, 0.3);
    fcl::CollisionObjectd on_voxel(probe, at(1.0, 0, 0));
    fcl::CollisionObjectd off_voxel(probe, at(3.0, 0, 0));
    check(collide(octree, on_voxel), "box on an occupied voxel collides");
    check(!collide(octree, off_voxel), "box in free space does not");
  }

  // A broadphase manager finds the one overlapping pair of three spheres.
  {
    fcl::CollisionObjectd a(sphere, at(0, 0, 0));
    fcl::CollisionObjectd b(sphere, at(1.5, 0, 0));
    fcl::CollisionObjectd c(sphere, at(10, 0, 0));
    std::vector<fcl::CollisionObjectd*> objects{&a, &b, &c};
    fcl::DynamicAABBTreeCollisionManagerd manager;
    manager.registerObjects(objects);
    manager.setup();
    fcl::DefaultCollisionData<double> data;
    data.request.num_max_contacts = 10;
    manager.collide(&data, fcl::DefaultCollisionFunction<double>);
    check(data.result.isCollision(), "broadphase finds a collision");
    check(data.result.numContacts() == 1, "broadphase finds one pair");
  }

  if (failures == 0) {
    std::puts("fcl: ok");
  }
  return failures == 0 ? 0 : 1;
}
