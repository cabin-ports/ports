// Parses binary glTF (GLB) built in memory, with a sparse accessor that
// overrides one vertex, writes it back to JSON with the sparse accessor
// intact, and rejects malformed GLB containers; then loads a .gltf file and
// its external buffer through custom file and memory callbacks, checking the
// buffer path cgltf resolves and decodes and that cgltf_free returns every
// allocation and file.
#define CGLTF_IMPLEMENTATION
#define CGLTF_WRITE_IMPLEMENTATION
#include <cgltf_write.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, int line, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "main.cc:%d: check failed: %s\n", line, what);
    ++failures;
  }
}

#define CHECK(expr) check((expr), __LINE__, #expr)

bool approx(float a, float b) { return a - b < 1e-6f && b - a < 1e-6f; }

void put_u32(std::string& out, std::uint32_t v) {
  for (int i = 0; i < 4; ++i) out += static_cast<char>((v >> (8 * i)) & 0xff);
}

void put_f32(std::string& out, float f) {
  std::uint32_t bits;
  std::memcpy(&bits, &f, 4);
  put_u32(out, bits);
}

// Vertices (0,0,0), (1,0,0), (0,2,0) in bytes 0-35 and uint16 indices 0, 1,
// 2 in bytes 36-41, then two bytes of padding.
std::string mesh_bytes() {
  std::string out;
  const float vertices[] = {0, 0, 0, 1, 0, 0, 0, 2, 0};
  for (float v : vertices) put_f32(out, v);
  const char indices[] = {0, 0, 1, 0, 2, 0, 0, 0};
  out.append(indices, sizeof indices);
  return out;
}

std::string glb(const std::string& json, const std::string& bin, std::uint32_t version = 2) {
  std::string padded = json;
  while (padded.size() % 4 != 0) padded += ' ';
  std::string out;
  put_u32(out, 0x46546C67);  // "glTF"
  put_u32(out, version);
  put_u32(out, static_cast<std::uint32_t>(12 + 8 + padded.size() + 8 + bin.size()));
  put_u32(out, static_cast<std::uint32_t>(padded.size()));
  put_u32(out, 0x4E4F534A);  // "JSON"
  out += padded;
  put_u32(out, static_cast<std::uint32_t>(bin.size()));
  put_u32(out, 0x004E4942);  // "BIN\0"
  out += bin;
  return out;
}

void binary_gltf() {
  // Accessor 0 reads the three vertices from view 0; accessor 2 does the
  // same but its sparse substitution moves vertex 1 to (5,5,5): index 1 as
  // a uint8 in view 2 (byte 44), the value in view 3 (bytes 48-59).
  std::string bin = mesh_bytes();
  bin += std::string(1, '\x01') + std::string(3, '\0');
  for (int i = 0; i < 3; ++i) put_f32(bin, 5);
  const std::string json =
      R"({"asset": {"version": "2.0"}, "buffers": [{"byteLength": 60}],
          "bufferViews": [{"buffer": 0, "byteLength": 36},
                          {"buffer": 0, "byteOffset": 36, "byteLength": 6},
                          {"buffer": 0, "byteOffset": 44, "byteLength": 1},
                          {"buffer": 0, "byteOffset": 48, "byteLength": 12}],
          "accessors": [{"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3"},
                        {"bufferView": 1, "componentType": 5123, "count": 3, "type": "SCALAR"},
                        {"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3",
                         "sparse": {"count": 1,
                                    "indices": {"bufferView": 2, "componentType": 5121},
                                    "values": {"bufferView": 3}}}],
          "meshes": [{"primitives": [{"attributes": {"POSITION": 2}, "indices": 1}]}],
          "nodes": [{"mesh": 0}], "scenes": [{"nodes": [0]}]})";
  const std::string file = glb(json, bin);

  cgltf_options options = {};
  cgltf_data* data = nullptr;
  CHECK(cgltf_parse(&options, file.data(), file.size(), &data) == cgltf_result_success);
  if (data == nullptr) return;
  CHECK(data->file_type == cgltf_file_type_glb && data->bin_size == 60);
  CHECK(cgltf_load_buffers(&options, data, nullptr) == cgltf_result_success);
  CHECK(data->buffers[0].data == data->bin);
  CHECK(cgltf_validate(data) == cgltf_result_success);

  CHECK(data->accessors_count == 3 && !data->accessors[0].is_sparse);
  const cgltf_accessor* sparse = &data->accessors[2];
  CHECK(sparse->is_sparse && sparse->sparse.count == 1 &&
        sparse->sparse.indices_component_type == cgltf_component_type_r_8u);
  CHECK(data->meshes[0].primitives[0].attributes[0].data == sparse);

  float base[9] = {};
  float merged[9] = {};
  float element[3] = {};
  CHECK(cgltf_accessor_unpack_floats(&data->accessors[0], base, 9) == 9);
  CHECK(cgltf_accessor_unpack_floats(sparse, merged, 9) == 9);
  for (int i = 0; i < 9; ++i) CHECK(approx(merged[i], i / 3 == 1 ? 5.0f : base[i]));
  CHECK(approx(base[7], 2) && approx(base[3], 1));
  // Element reads do not apply sparse data and refuse sparse accessors.
  CHECK(!cgltf_accessor_read_float(sparse, 1, element, 3));
  CHECK(cgltf_accessor_read_float(&data->accessors[0], 1, element, 3) && approx(element[0], 1));

  // Widened from uint16 to uint32 on the way out.
  std::uint32_t indices[3] = {9, 9, 9};
  CHECK(cgltf_accessor_unpack_indices(&data->accessors[1], indices, 4, 3) == 3);
  CHECK(indices[0] == 0 && indices[1] == 1 && indices[2] == 2);

  // The written JSON keeps the sparse substitution's structure.
  const cgltf_size json_size = cgltf_write(&options, nullptr, 0, data);
  std::vector<char> written(json_size);
  CHECK(json_size > 1 && cgltf_write(&options, written.data(), json_size, data) == json_size);
  cgltf_free(data);
  data = nullptr;
  CHECK(cgltf_parse(&options, written.data(), json_size - 1, &data) == cgltf_result_success);
  if (data == nullptr) return;
  CHECK(data->file_type == cgltf_file_type_gltf && data->accessors_count == 3);
  const cgltf_accessor_sparse& copy = data->accessors[2].sparse;
  CHECK(data->accessors[2].is_sparse && copy.count == 1 &&
        copy.indices_component_type == cgltf_component_type_r_8u &&
        copy.indices_buffer_view == &data->buffer_views[2] &&
        copy.values_buffer_view == &data->buffer_views[3]);
  cgltf_free(data);

  // The JSON chunk claims a 64-byte buffer, which the 60-byte BIN chunk
  // cannot supply.
  std::string short_json = json;
  short_json.replace(short_json.find("60"), 2, "64");
  const std::string short_file = glb(short_json, bin);
  data = nullptr;
  CHECK(cgltf_parse(&options, short_file.data(), short_file.size(), &data) ==
        cgltf_result_success);
  if (data != nullptr) {
    CHECK(cgltf_load_buffers(&options, data, nullptr) == cgltf_result_data_too_short);
    cgltf_free(data);
  }

  CHECK(cgltf_parse(&options, file.data(), file.size() - 1, &data) == cgltf_result_data_too_short);
  const std::string version1 = glb(json, bin, 1);
  CHECK(cgltf_parse(&options, version1.data(), version1.size(), &data) == cgltf_result_legacy_gltf);
  std::string bad_chunk = file;
  bad_chunk[16] = 'X';  // the JSON chunk's type
  CHECK(cgltf_parse(&options, bad_chunk.data(), bad_chunk.size(), &data) ==
        cgltf_result_unknown_format);
}

struct Storage {
  std::map<std::string, std::string> files;
  std::vector<std::string> opened;
  int live_allocations = 0;
  int releases = 0;
};

void* counted_alloc(void* user, cgltf_size size) {
  ++static_cast<Storage*>(user)->live_allocations;
  return std::malloc(size);
}

void counted_free(void* user, void* ptr) {
  if (ptr == nullptr) return;
  --static_cast<Storage*>(user)->live_allocations;
  std::free(ptr);
}

cgltf_result read_file(const cgltf_memory_options* memory, const cgltf_file_options* file,
                       const char* path, cgltf_size* size, void** data) {
  Storage& storage = *static_cast<Storage*>(file->user_data);
  storage.opened.push_back(path);
  const auto it = storage.files.find(path);
  if (it == storage.files.end()) return cgltf_result_file_not_found;
  if (*size != 0 && *size > it->second.size()) return cgltf_result_io_error;
  void* copy = memory->alloc_func(memory->user_data, it->second.size());
  std::memcpy(copy, it->second.data(), it->second.size());
  *size = it->second.size();
  *data = copy;
  return cgltf_result_success;
}

void release_file(const cgltf_memory_options* memory, const cgltf_file_options* file, void* data) {
  ++static_cast<Storage*>(file->user_data)->releases;
  memory->free_func(memory->user_data, data);
}

void external_buffer() {
  Storage storage;
  // cgltf resolves the URI against the .gltf's directory and decodes %20.
  storage.files["assets/scene.gltf"] =
      R"({"asset": {"version": "2.0"},
          "buffers": [{"byteLength": 44, "uri": "mesh%20data.bin"}],
          "bufferViews": [{"buffer": 0, "byteLength": 36},
                          {"buffer": 0, "byteOffset": 36, "byteLength": 6}],
          "accessors": [{"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3"},
                        {"bufferView": 1, "componentType": 5123, "count": 3, "type": "SCALAR"}],
          "meshes": [{"primitives": [{"attributes": {"POSITION": 0}, "indices": 1}]}]})";
  storage.files["assets/mesh data.bin"] = mesh_bytes();

  cgltf_options options = {};
  options.memory.alloc_func = counted_alloc;
  options.memory.free_func = counted_free;
  options.memory.user_data = &storage;
  options.file.read = read_file;
  options.file.release = release_file;
  options.file.user_data = &storage;

  cgltf_data* data = nullptr;
  CHECK(cgltf_parse_file(&options, "assets/scene.gltf", &data) == cgltf_result_success);
  if (data == nullptr) return;
  CHECK(cgltf_load_buffers(&options, data, "assets/scene.gltf") == cgltf_result_success);
  CHECK(storage.opened.size() == 2 && storage.opened[1] == "assets/mesh data.bin");
  CHECK(cgltf_validate(data) == cgltf_result_success);
  CHECK(storage.live_allocations > 0);

  float vertices[9] = {};
  CHECK(cgltf_accessor_unpack_floats(&data->accessors[0], vertices, 9) == 9);
  CHECK(approx(vertices[3], 1) && approx(vertices[7], 2));
  CHECK(cgltf_accessor_read_index(&data->accessors[1], 2) == 2);

  cgltf_free(data);
  CHECK(storage.releases == 2);  // the .gltf and the .bin
  CHECK(storage.live_allocations == 0);

  data = nullptr;
  CHECK(cgltf_parse_file(&options, "assets/missing.gltf", &data) == cgltf_result_file_not_found);
  CHECK(data == nullptr && storage.live_allocations == 0);

  // A buffer whose file is missing fails the load; cgltf_free still frees
  // everything parsing allocated.
  storage.files.erase("assets/mesh data.bin");
  CHECK(cgltf_parse_file(&options, "assets/scene.gltf", &data) == cgltf_result_success);
  if (data == nullptr) return;
  CHECK(cgltf_load_buffers(&options, data, "assets/scene.gltf") == cgltf_result_file_not_found);
  cgltf_free(data);
  CHECK(storage.live_allocations == 0);
}

}  // namespace

int main() {
  binary_gltf();
  external_buffer();
  std::printf("cgltf (C++): %s\n", failures == 0 ? "ok" : "FAILED");
  return failures == 0 ? 0 : 1;
}
