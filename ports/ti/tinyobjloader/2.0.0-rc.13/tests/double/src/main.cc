#include <cmath>
#include <cstdio>
#include <string>

#include "tiny_obj_loader.h"

int main() {
  static_assert(sizeof(tinyobj::real_t) == sizeof(double), "double build");

  // A float 0.1 is off by about 1.5e-9, so a float library fails the tolerance.
  tinyobj::ObjReader reader;
  if (!reader.ParseFromString("v 0.1 0.2 0.3\nf 1 1 1\n", "")) return 1;
  const tinyobj::attrib_t &a = reader.GetAttrib();
  if (a.vertices.size() != 3 || std::fabs(a.vertices[0] - 0.1) > 1e-12 ||
      std::fabs(a.vertices[2] - 0.3) > 1e-12) {
    std::fprintf(stderr, "unexpected vertices\n");
    return 1;
  }
  return 0;
}
