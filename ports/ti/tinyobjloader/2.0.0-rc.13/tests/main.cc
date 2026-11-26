#include <cstdio>
#include <string>

#include "tiny_obj_loader.h"

static int failures = 0;
#define CHECK(c)                                          \
  do {                                                    \
    if (!(c)) {                                           \
      std::fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #c); \
      ++failures;                                         \
    }                                                     \
  } while (0)

int main() {
  static_assert(sizeof(tinyobj::real_t) == sizeof(float), "float build");

  const std::string obj =
      "# CRLF endings exercise the line reader\r\n"
      "v 0 0 0\r\nv 2 0 0\r\nv 2 3 0\r\nv 0 3 0\r\n"
      "vt 0 0\r\nvt 1 0\r\nvt 1 1\r\nvt 0 1\r\n"
      "vn 0 0 1\r\n"
      "mtllib plate.mtl\r\n"
      "o tri\r\n"
      "f 1 2 3\r\n"
      "o plate\r\n"
      "usemtl red\r\n"
      "f 1/1/1 2/2/1 3/3/1 4/4/1\r\n";
  const std::string mtl =
      "newmtl red\nKd 1 0 0\nNs 32\nmap_Kd red.png\n";

  tinyobj::ObjReader reader;
  CHECK(reader.ParseFromString(obj, mtl));
  CHECK(reader.Valid());

  const tinyobj::attrib_t &a = reader.GetAttrib();
  CHECK(a.vertices.size() == 12);
  CHECK(a.vertices[6] == 2.0f && a.vertices[7] == 3.0f);
  CHECK(a.normals.size() == 3 && a.normals[2] == 1.0f);
  CHECK(a.texcoords.size() == 8 && a.texcoords[2] == 1.0f &&
        a.texcoords[3] == 0.0f && a.texcoords[7] == 1.0f);

  const std::vector<tinyobj::shape_t> &shapes = reader.GetShapes();
  CHECK(shapes.size() == 2);
  if (shapes.size() == 2) {
    CHECK(shapes[0].name == "tri");
    CHECK(shapes[0].mesh.num_face_vertices.size() == 1);
    CHECK(shapes[0].mesh.material_ids[0] == -1);
    CHECK(shapes[1].name == "plate");
    // The quad is triangulated into two faces.
    CHECK(shapes[1].mesh.num_face_vertices.size() == 2);
    CHECK(shapes[1].mesh.indices.size() == 6);
    CHECK(shapes[1].mesh.material_ids[0] == 0);
    CHECK(shapes[1].mesh.indices[1].vertex_index == 1);
    CHECK(shapes[1].mesh.indices[1].texcoord_index == 1);
    CHECK(shapes[1].mesh.indices[1].normal_index == 0);
  }

  const std::vector<tinyobj::material_t> &m = reader.GetMaterials();
  CHECK(m.size() == 1);
  if (m.size() == 1) {
    CHECK(m[0].name == "red");
    CHECK(m[0].diffuse[0] == 1.0f && m[0].diffuse[1] == 0.0f);
    CHECK(m[0].shininess == 32.0f);
    CHECK(m[0].diffuse_texname == "red.png");
  }

  tinyobj::ObjReader bad;
  CHECK(!bad.ParseFromFile("no-such-file.obj"));
  CHECK(!bad.Error().empty());

  return failures == 0 ? 0 : 1;
}
