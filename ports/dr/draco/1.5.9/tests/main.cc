#include "draco/compression/decode.h"
#include "draco/compression/encode.h"
#include "draco/draco_features.h"
#include "draco/io/obj_decoder.h"
#include "draco/io/ply_decoder.h"
#include "draco/io/ply_encoder.h"
#include "draco/mesh/mesh.h"
#include "draco/metadata/geometry_metadata.h"
#include "draco/point_cloud/point_cloud_builder.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <set>
#include <string>

// The default features CMake writes into draco_features.h.
#if !defined(DRACO_POINT_CLOUD_COMPRESSION_SUPPORTED) || !defined(DRACO_MESH_COMPRESSION_SUPPORTED) || \
    !defined(DRACO_NORMAL_ENCODING_SUPPORTED) || !defined(DRACO_STANDARD_EDGEBREAKER_SUPPORTED) ||      \
    !defined(DRACO_PREDICTIVE_EDGEBREAKER_SUPPORTED) || !defined(DRACO_BACKWARDS_COMPATIBILITY_SUPPORTED) || \
    !defined(DRACO_ATTRIBUTE_INDICES_DEDUPLICATION_SUPPORTED) ||                                         \
    !defined(DRACO_ATTRIBUTE_VALUES_DEDUPLICATION_SUPPORTED) || defined(DRACO_TRANSCODER_SUPPORTED)
#error "draco_features.h does not match upstream's default options"
#endif

static int failures = 0;

static void check(bool ok, const char* expr, int line) {
  if (!ok) {
    std::fprintf(stderr, "main.cc:%d: CHECK(%s) failed\n", line, expr);
    ++failures;
  }
}

#define CHECK(...) check((__VA_ARGS__), #__VA_ARGS__, __LINE__)

// A 2 x 3 x 4 box with outward-facing triangles: volume 24, area 52.
static const char kBoxObj[] =
    "v -1 0.5 2\nv 1 0.5 2\nv 1 3.5 2\nv -1 3.5 2\n"
    "v -1 0.5 6\nv 1 0.5 6\nv 1 3.5 6\nv -1 3.5 6\n"
    "f 1 3 2\nf 1 4 3\nf 5 6 7\nf 5 7 8\nf 1 2 6\nf 1 6 5\n"
    "f 4 8 7\nf 4 7 3\nf 1 5 8\nf 1 8 4\nf 2 3 7\nf 2 7 6\n";

struct Shape {
  double volume = 0;
  double area = 0;
  double max_corner_error = 0;  // distance from the nearest box coordinate
};

static double corner_error(float v, float lo, float hi) {
  return std::fmin(std::fabs(v - lo), std::fabs(v - hi));
}

static Shape measure(const draco::Mesh& mesh) {
  Shape s;
  const draco::PointAttribute* pos = mesh.GetNamedAttribute(draco::GeometryAttribute::POSITION);
  if (pos == nullptr) {
    return s;
  }
  for (uint32_t i = 0; i < mesh.num_faces(); ++i) {
    const draco::Mesh::Face& face = mesh.face(draco::FaceIndex(i));
    float p[3][3];
    for (int c = 0; c < 3; ++c) {
      pos->ConvertValue<float>(pos->mapped_index(face[c]), p[c]);
      s.max_corner_error = std::fmax(s.max_corner_error, corner_error(p[c][0], -1, 1));
      s.max_corner_error = std::fmax(s.max_corner_error, corner_error(p[c][1], 0.5f, 3.5f));
      s.max_corner_error = std::fmax(s.max_corner_error, corner_error(p[c][2], 2, 6));
    }
    double u[3], v[3];
    for (int k = 0; k < 3; ++k) {
      u[k] = p[1][k] - p[0][k];
      v[k] = p[2][k] - p[0][k];
    }
    double n[3] = {u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]};
    s.area += std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]) / 2;
    s.volume += (p[0][0] * n[0] + p[0][1] * n[1] + p[0][2] * n[2]) / 6;
  }
  return s;
}

static bool is_box(const draco::Mesh& mesh, double eps) {
  Shape s = measure(mesh);
  return mesh.num_faces() == 12 && mesh.num_points() == 8 && std::fabs(s.volume - 24) <= 64 * eps &&
         std::fabs(s.area - 52) <= 64 * eps && s.max_corner_error <= eps;
}

static std::unique_ptr<draco::Mesh> decode_mesh(const draco::EncoderBuffer& encoded) {
  draco::DecoderBuffer in;
  in.Init(encoded.data(), encoded.size());
  draco::Decoder decoder;
  auto mesh = decoder.DecodeMeshFromBuffer(&in);
  return mesh.ok() ? std::move(mesh).value() : nullptr;
}

// The 11-byte header: "DRACO", bitstream version, geometry type, method, and
// 16 flag bits in host byte order, of which metadata is 0x8000.
static bool header_is(const draco::EncoderBuffer& b, int geometry, int method, bool metadata) {
  const unsigned char* h = reinterpret_cast<const unsigned char*>(b.data());
  return b.size() > 11 && std::memcmp(h, "DRACO", 5) == 0 && h[5] == 2 &&
         h[6] == (geometry == draco::POINT_CLOUD ? 3 : 2) && h[7] == geometry && h[8] == method &&
         (h[9] | h[10]) == (metadata ? 0x80 : 0);
}

static void check_meshes() {
  draco::DecoderBuffer obj;
  obj.Init(kBoxObj, sizeof kBoxObj - 1);
  draco::Mesh box;
  CHECK(draco::ObjDecoder().DecodeFromBuffer(&obj, &box).ok());
  // OBJ corners become 8 points only through attribute deduplication.
  CHECK(is_box(box, 1e-6));

  // Edgebreaker with each connectivity coder, which it writes right after
  // the header; 14-bit positions: a step of 4/16383 over the largest extent.
  // Unforced, a mesh under 1000 faces always gets the standard coder.
  for (int coder : {draco::MESH_EDGEBREAKER_STANDARD_ENCODING, draco::MESH_EDGEBREAKER_VALENCE_ENCODING}) {
    draco::Encoder encoder;
    encoder.SetEncodingMethod(draco::MESH_EDGEBREAKER_ENCODING);
    encoder.options().SetGlobalInt("edgebreaker_method", coder);
    encoder.SetAttributeQuantization(draco::GeometryAttribute::POSITION, 14);
    draco::EncoderBuffer encoded;
    CHECK(encoder.EncodeMeshToBuffer(box, &encoded).ok());
    CHECK(header_is(encoded, draco::TRIANGULAR_MESH, draco::MESH_EDGEBREAKER_ENCODING, false));
    CHECK(encoded.size() > 11 && static_cast<unsigned char>(encoded.data()[11]) == coder);

    draco::DecoderBuffer in;
    in.Init(encoded.data(), encoded.size());
    auto type = draco::Decoder::GetEncodedGeometryType(&in);
    CHECK(type.ok() && type.value() == draco::TRIANGULAR_MESH);

    std::unique_ptr<draco::Mesh> decoded = decode_mesh(encoded);
    CHECK(decoded != nullptr);
    if (decoded) {
      CHECK(is_box(*decoded, 2.5e-4));
      CHECK(!is_box(*decoded, 1e-9));  // quantization is lossy
    }
  }

  // Sequential connectivity with unquantized positions is lossless, and
  // geometry metadata travels with the bitstream.
  std::unique_ptr<draco::GeometryMetadata> metadata(new draco::GeometryMetadata());
  metadata->AddEntryString("name", "box");
  metadata->AddEntryInt("faces", 12);
  box.AddMetadata(std::move(metadata));
  draco::Encoder encoder;
  encoder.SetSpeedOptions(10, 10);
  encoder.SetEncodingMethod(draco::MESH_SEQUENTIAL_ENCODING);
  draco::EncoderBuffer encoded;
  CHECK(encoder.EncodeMeshToBuffer(box, &encoded).ok());
  CHECK(header_is(encoded, draco::TRIANGULAR_MESH, draco::MESH_SEQUENTIAL_ENCODING, true));
  std::unique_ptr<draco::Mesh> decoded = decode_mesh(encoded);
  CHECK(decoded != nullptr && is_box(*decoded, 0));
  if (decoded) {
    const draco::GeometryMetadata* m = decoded->GetMetadata();
    std::string name;
    int32_t faces = 0;
    CHECK(m != nullptr && m->GetEntryString("name", &name) && name == "box");
    CHECK(m != nullptr && m->GetEntryInt("faces", &faces) && faces == 12);
  }

  // PLY through io/.
  draco::EncoderBuffer ply;
  CHECK(draco::PlyEncoder().EncodeToBuffer(box, &ply));
  draco::DecoderBuffer ply_in;
  ply_in.Init(ply.data(), ply.size());
  draco::Mesh from_ply;
  CHECK(draco::PlyDecoder().DecodeFromBuffer(&ply_in, &from_ply).ok());
  CHECK(is_box(from_ply, 0));

  // Corrupt input fails cleanly.
  static const char kGarbage[] = "DRACO but not really a draco bitstream";
  draco::DecoderBuffer garbage;
  garbage.Init(kGarbage, sizeof kGarbage - 1);
  draco::Decoder decoder;
  CHECK(!decoder.DecodeMeshFromBuffer(&garbage).ok());
  garbage.Init("nope", 4);
  CHECK(!draco::Decoder::GetEncodedGeometryType(&garbage).ok());
}

// A 10 x 10 x 10 grid of points, each carrying its own id; the k-d tree
// encoder reorders points, so ids must still follow their positions.
static void check_point_cloud() {
  draco::PointCloudBuilder builder;
  builder.Start(1000);
  const int pos = builder.AddAttribute(draco::GeometryAttribute::POSITION, 3, draco::DT_FLOAT32);
  const int id = builder.AddAttribute(draco::GeometryAttribute::GENERIC, 1, draco::DT_UINT16);
  for (int i = 0; i < 1000; ++i) {
    const float p[3] = {float(i / 100), float(i / 10 % 10), float(i % 10)};
    const uint16_t v = static_cast<uint16_t>(i);
    builder.SetAttributeValueForPoint(pos, draco::PointIndex(i), p);
    builder.SetAttributeValueForPoint(id, draco::PointIndex(i), &v);
  }
  std::unique_ptr<draco::PointCloud> cloud = builder.Finalize(false);

  draco::Encoder encoder;
  encoder.SetSpeedOptions(0, 0);
  encoder.SetEncodingMethod(draco::POINT_CLOUD_KD_TREE_ENCODING);
  encoder.SetAttributeQuantization(draco::GeometryAttribute::POSITION, 10);
  draco::EncoderBuffer encoded;
  CHECK(encoder.EncodePointCloudToBuffer(*cloud, &encoded).ok());
  CHECK(header_is(encoded, draco::POINT_CLOUD, draco::POINT_CLOUD_KD_TREE_ENCODING, false));

  draco::DecoderBuffer in;
  in.Init(encoded.data(), encoded.size());
  draco::Decoder decoder;
  auto result = decoder.DecodePointCloudFromBuffer(&in);
  CHECK(result.ok());
  if (!result.ok()) {
    return;
  }
  std::unique_ptr<draco::PointCloud> decoded = std::move(result).value();
  CHECK(decoded->num_points() == 1000);
  const draco::PointAttribute* dpos = decoded->GetNamedAttribute(draco::GeometryAttribute::POSITION);
  const draco::PointAttribute* did = decoded->GetNamedAttribute(draco::GeometryAttribute::GENERIC);
  CHECK(dpos != nullptr && did != nullptr);
  if (dpos == nullptr || did == nullptr) {
    return;
  }
  std::set<int> ids;
  int mismatches = 0;
  for (uint32_t i = 0; i < decoded->num_points(); ++i) {
    float p[3];
    uint16_t v = 0;
    dpos->ConvertValue<float>(dpos->mapped_index(draco::PointIndex(i)), p);
    did->ConvertValue<uint16_t>(did->mapped_index(draco::PointIndex(i)), &v);
    const long x = std::lround(p[0]), y = std::lround(p[1]), z = std::lround(p[2]);
    if (std::fabs(p[0] - x) > 0.01 || std::fabs(p[1] - y) > 0.01 || std::fabs(p[2] - z) > 0.01 ||
        v != x * 100 + y * 10 + z) {
      ++mismatches;
    }
    ids.insert(v);
  }
  CHECK(mismatches == 0);
  CHECK(ids.size() == 1000 && *ids.begin() == 0 && *ids.rbegin() == 999);
}

int main() {
  check_meshes();
  check_point_cloud();
  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("ok");
  return 0;
}
