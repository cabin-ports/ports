#include <octomap/AbstractOcTree.h>
#include <octomap/ColorOcTree.h>
#include <octomap/CountingOcTree.h>
#include <octomap/MapCollection.h>
#include <octomap/MapNode.h>
#include <octomap/OcTree.h>
#include <octomap/math/Pose6D.h>
#include <octomap/math/Utils.h>

#include <cmath>
#include <cstdio>
#include <memory>
#include <sstream>
#include <string>

using octomap::point3d;

static int failures = 0;

static void check(bool ok, const char* expr, int line) {
  if (!ok) {
    std::fprintf(stderr, "main.cc:%d: CHECK(%s) failed\n", line, expr);
    ++failures;
  }
}

#define CHECK(...) check((__VA_ARGS__), #__VA_ARGS__, __LINE__)

static bool near(double a, double b, double eps = 1e-5) {
  return std::fabs(a - b) <= eps;
}

static bool near(const point3d& a, const point3d& b) {
  return near(a.x(), b.x()) && near(a.y(), b.y()) && near(a.z(), b.z());
}

// Center of the voxel i cells along +x and j along +y from the one holding
// (0.05, 0.05, 0.05) in a 0.1 m tree.
static point3d cell(int i, int j = 0) {
  return point3d(0.05f + 0.1f * static_cast<float>(i),
                 0.05f + 0.1f * static_cast<float>(j), 0.05f);
}

// octomath, with the values upstream's unit_tests.cpp expects.
static void check_math() {
  octomath::Vector3 ones(1, 1, 1);
  octomath::Vector3 twos(2, 2, 2);
  CHECK(near((twos - ones).x(), 1) && near((twos + ones).y(), 3));
  CHECK(near(ones.dot(twos), 6));
  CHECK(near(octomath::Vector3(1, 0, 0).cross(octomath::Vector3(0, 1, 0)).z(), 1));
  CHECK(ones < twos && !(twos < ones));

  octomath::Vector3 rotation = ones;
  rotation.rotate_IP(M_PI, 1., 0.1);
  CHECK(near(rotation, point3d(1.2750367f, -1.1329513f, 0.30116868f)));

  octomath::Vector3 trans(1.0f, 0.1f, 0.1f);
  octomath::Pose6D pose(trans, octomath::Quaternion(0.0f, 0.1f, float(M_PI / 4.)));
  CHECK(pose == octomath::Pose6D(1.0f, 0.1f, 0.1f, 0.0f, 0.1f, float(M_PI / 4.)));
  CHECK(near(pose.yaw(), M_PI / 4.));
  octomath::Vector3 moved = pose.transform(trans);
  CHECK(near(moved, point3d(1.6399229f, 0.8813442f, 0.099667005f)));
  CHECK(near(pose.inv().transform(moved), trans));
}

// Keys, occupancy updates, clamping, automatic pruning, and expansion.
static void check_updates_and_pruning() {
  octomap::OcTree tree(0.1);
  CHECK(tree.getTreeDepth() == 16);
  CHECK(tree.coordToKey(point3d(0.05f, 0.05f, 0.05f)) == octomap::OcTreeKey(32768, 32768, 32768));
  CHECK(tree.coordToKey(point3d(-0.05f, 0.05f, 0.05f))[0] == 32767);
  CHECK(near(tree.keyToCoord(octomap::OcTreeKey(32770, 32768, 32767)), point3d(0.25f, 0.05f, -0.05f)));

  octomap::OcTreeNode* node = tree.updateNode(cell(0), true);
  CHECK(node != nullptr && node == tree.search(cell(0)));
  CHECK(near(node->getOccupancy(), 0.7));
  CHECK(tree.isNodeOccupied(node) && !tree.isNodeAtThreshold(node));
  CHECK(tree.size() == 17 && tree.getNumLeafNodes() == 1);  // root + 16 levels
  for (int i = 0; i < 4; ++i) {
    node = tree.updateNode(cell(0), true);
  }
  // Five hits saturate at the 0.971 upper clamping threshold.
  CHECK(tree.isNodeAtThreshold(node));
  CHECK(near(node->getLogOdds(), octomap::logodds(0.971), 1e-4));
  CHECK(tree.search(cell(0, 2)) == nullptr);

  // Eight equal siblings collapse into their parent once the last arrives.
  octomap::OcTree cube(0.1);
  octomap::OcTreeKey base = cube.coordToKey(cell(0));
  for (int n = 0; n < 8; ++n) {
    octomap::OcTreeKey key(base[0] + (n & 1), base[1] + ((n >> 1) & 1), base[2] + (n >> 2));
    cube.updateNode(key, true);
    if (n == 6) {
      CHECK(cube.size() == 23 && cube.getNumLeafNodes() == 7);
    }
  }
  CHECK(cube.size() == 16 && cube.getNumLeafNodes() == 1);
  CHECK(cube.calcNumNodes() == cube.size());
  octomap::OcTreeNode* parent = cube.search(cell(1, 1));
  CHECK(parent != nullptr && cube.isNodeOccupied(parent) && !cube.nodeHasChildren(parent));
  cube.expand();
  CHECK(cube.size() == 24 && cube.getNumLeafNodes() == 8);
  cube.prune();
  CHECK(cube.size() == 16 && cube.getNumLeafNodes() == 1);
}

// A ray from cell 0 to cell 10: cells 0-9 free, cell 10 occupied.
static void check_rays_and_io() {
  octomap::OcTree tree(0.1);
  CHECK(tree.insertRay(cell(0), cell(10)));
  CHECK(tree.getNumLeafNodes() == 11);
  for (int i = 0; i < 10; ++i) {
    octomap::OcTreeNode* node = tree.search(cell(i));
    CHECK(node != nullptr && !tree.isNodeOccupied(node) && near(node->getOccupancy(), 0.4));
  }
  CHECK(tree.search(cell(10)) != nullptr && tree.isNodeOccupied(tree.search(cell(10))));
  CHECK(tree.search(cell(11)) == nullptr);

  point3d end;
  CHECK(tree.castRay(cell(0), point3d(1, 0, 0), end));
  CHECK(near(end, cell(10)));
  CHECK(!tree.castRay(cell(0), point3d(-1, 0, 0), end));  // unknown cell
  CHECK(near(end, cell(-1)) && tree.search(end) == nullptr);
  CHECK(!tree.castRay(cell(0), point3d(1, 0, 0), end, false, 0.55));  // out of range

  // The compact binary format stores the maximum-likelihood map.
  std::stringstream binary;
  CHECK(tree.writeBinary(binary));
  octomap::OcTree loaded(0.5);
  CHECK(loaded.readBinary(binary));
  CHECK(near(loaded.getResolution(), 0.1) && loaded.getNumLeafNodes() == 11);
  for (int i = 0; i < 10; ++i) {
    octomap::OcTreeNode* node = loaded.search(cell(i));
    CHECK(node != nullptr && near(node->getLogOdds(), loaded.getClampingThresMinLog()));
  }
  octomap::OcTreeNode* hit = loaded.search(cell(10));
  CHECK(hit != nullptr && near(hit->getLogOdds(), loaded.getClampingThresMaxLog()));
}

// A two-point scan: the union of both rays is free, the end points occupied.
static void check_point_cloud() {
  octomap::Pointcloud scan;
  scan.push_back(cell(10));
  scan.push_back(cell(0, 10));
  CHECK(scan.size() == 2);

  octomap::OcTree tree(0.1);
  tree.insertPointCloud(scan, cell(0));
  CHECK(tree.getNumLeafNodes() == 21);
  octomap::OcTreeNode* end = tree.search(cell(0, 10));
  octomap::OcTreeNode* before = tree.search(cell(0, 9));
  CHECK(end != nullptr && tree.isNodeOccupied(end));
  CHECK(before != nullptr && !tree.isNodeOccupied(before));

  // Leaves in [0, 0.48] x [0, 0.48] x [0, 0.08]: cells 0-4 along x, 1-4 along y.
  int leaves = 0;
  for (octomap::OcTree::leaf_bbx_iterator it = tree.begin_leafs_bbx(point3d(0, 0, 0), point3d(0.48f, 0.48f, 0.08f)),
                                          last = tree.end_leafs_bbx();
       it != last; ++it) {
    CHECK(it.getX() < 0.5 && it.getY() < 0.5 && !tree.isNodeOccupied(*it));
    ++leaves;
  }
  CHECK(leaves == 9);
}

// Colors, and the full .ot format through AbstractOcTree's tree factory.
static void check_color_tree() {
  octomap::ColorOcTree tree(0.1);
  tree.updateNode(cell(0), true);
  tree.setNodeColor(cell(0).x(), cell(0).y(), cell(0).z(), 255, 0, 0);
  octomap::ColorOcTreeNode* node = tree.averageNodeColor(cell(0).x(), cell(0).y(), cell(0).z(), 0, 0, 255);
  CHECK(node != nullptr && node->getColor() == octomap::ColorOcTreeNode::Color(127, 0, 127));
  tree.updateInnerOccupancy();
  CHECK(tree.getRoot()->getColor() == octomap::ColorOcTreeNode::Color(127, 0, 127));

  std::stringstream full;
  CHECK(tree.write(full));
  std::unique_ptr<octomap::AbstractOcTree> read(octomap::AbstractOcTree::read(full));
  CHECK(read && read->getTreeType() == "ColorOcTree");
  octomap::ColorOcTree* loaded = dynamic_cast<octomap::ColorOcTree*>(read.get());
  CHECK(loaded != nullptr);
  if (loaded) {
    octomap::ColorOcTreeNode* back = loaded->search(cell(0));
    CHECK(back != nullptr && back->getColor() == octomap::ColorOcTreeNode::Color(127, 0, 127));
    CHECK(back != nullptr && near(back->getLogOdds(), octomap::logodds(0.7)));
  }
}

static void check_counting_tree() {
  octomap::CountingOcTree tree(0.1);
  for (int i = 0; i < 3; ++i) {
    tree.updateNode(cell(2));
  }
  tree.updateNode(cell(5));
  CHECK(tree.getRoot()->getCount() == 4);
  octomap::CountingOcTreeNode* leaf = tree.search(cell(2));
  CHECK(leaf != nullptr && leaf->getCount() == 3);
  octomap::point3d_list centers;
  tree.getCentersMinHits(centers, 3);
  CHECK(centers.size() == 1 && near(centers.front(), cell(2)));
}

// MapCollection resolves the maps a collection file lists against that
// file's directory, with the platform's separator.
struct Collection : octomap::MapCollection<octomap::MapNode<octomap::OcTree> > {
  using MapCollection::combinePathAndFilename;
  using MapCollection::splitPathAndFilename;
};

static void check_map_paths() {
#ifdef _WIN32
  std::string full = "maps\\run\\collection.txt";
  const char* joined = "maps\\run\\a.bt";
#else
  std::string full = "maps/run/collection.txt";
  const char* joined = "maps/run/a.bt";
#endif
  std::string dir;
  std::string file;
  Collection::splitPathAndFilename(full, &dir, &file);
  CHECK(file == "collection.txt");
  CHECK(Collection::combinePathAndFilename(dir, "a.bt") == joined);
}

// audit probe (never merged): a replacement for the patch must make this
// consumer TU see WIN32 exactly when it targets Windows.
static void audit_probe() {
#ifdef _WIN32
  const bool windows = true;
#else
  const bool windows = false;
#endif
#ifdef WIN32
  const bool win32 = true;
#else
  const bool win32 = false;
#endif
  std::printf("audit-probe: _WIN32=%d WIN32=%d join(\"d\", \"f\")=%s\n", windows, win32,
              Collection::combinePathAndFilename("d", "f").c_str());
  CHECK(win32 == windows);
}

int main() {
  check_math();
  check_updates_and_pruning();
  check_rays_and_io();
  check_point_cloud();
  check_color_tree();
  check_counting_tree();
  check_map_paths();
  audit_probe();
  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("ok");
  return 0;
}
