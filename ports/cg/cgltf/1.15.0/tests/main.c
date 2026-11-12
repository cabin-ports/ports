/* Parses a glTF document whose buffer is an embedded base64 data URI, loads
 * and validates it, reads its vertices and indices, computes a child node's
 * world transform, rejects malformed, legacy and inconsistent documents, and
 * round-trips the document through cgltf_write. */
#define CGLTF_IMPLEMENTATION
#define CGLTF_WRITE_IMPLEMENTATION
#include <cgltf_write.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

#define CHECK(expr)                                                   \
  do {                                                                \
    if (!(expr)) {                                                    \
      fprintf(stderr, "main.c:%d: check failed: %s\n", __LINE__, #expr); \
      ++failures;                                                     \
    }                                                                 \
  } while (0)

/* Vertices (0,0,0), (1,0,0) and (0,2,0) as float32 in bytes 0-35, then the
 * uint16 indices 0, 1, 2 in bytes 36-41 and two bytes of padding. */
static const char document[] =
    "{\"asset\": {\"version\": \"2.0\", \"generator\": \"cabin-ports test\"},"
    " \"buffers\": [{\"byteLength\": 44, \"uri\": \"data:application/octet-stream;base64,"
    "AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAAEAAAAAAAAABAAIAAAA=\"}],"
    " \"bufferViews\": [{\"buffer\": 0, \"byteLength\": 36, \"target\": 34962},"
    " {\"buffer\": 0, \"byteOffset\": 36, \"byteLength\": 6, \"target\": 34963}],"
    " \"accessors\": [{\"bufferView\": 0, \"componentType\": 5126, \"count\": 3,"
    " \"type\": \"VEC3\", \"min\": [0, 0, 0], \"max\": [1, 2, 0]},"
    " {\"bufferView\": 1, \"componentType\": 5123, \"count\": 3, \"type\": \"SCALAR\"}],"
    " \"meshes\": [{\"name\": \"triangle\", \"primitives\": [{\"attributes\": {\"POSITION\": 0},"
    " \"indices\": 1, \"mode\": 4}]}],"
    " \"nodes\": [{\"name\": \"root\", \"translation\": [1, 2, 3], \"children\": [1]},"
    " {\"name\": \"child\", \"scale\": [2, 2, 2], \"mesh\": 0}],"
    " \"scenes\": [{\"nodes\": [0]}], \"scene\": 0}";

static int approx(float a, float b) { return a - b < 1e-6f && b - a < 1e-6f; }

static cgltf_data* load(const char* json, cgltf_size size) {
  cgltf_options options;
  cgltf_data* data = NULL;
  memset(&options, 0, sizeof options);
  CHECK(cgltf_parse(&options, json, size, &data) == cgltf_result_success);
  if (data == NULL) return NULL;
  CHECK(data->file_type == cgltf_file_type_gltf);
  CHECK(cgltf_load_buffers(&options, data, NULL) == cgltf_result_success);
  CHECK(cgltf_validate(data) == cgltf_result_success);
  return data;
}

/* Checks everything the document above describes. */
static void inspect(const cgltf_data* data) {
  const cgltf_node* root;
  const cgltf_node* child;
  const cgltf_primitive* prim;
  const cgltf_accessor* positions;
  cgltf_float vertex[3];
  cgltf_float world[16];
  cgltf_float unpacked[9];
  cgltf_uint indices[3];
  static const cgltf_float expected_world[16] = {2, 0, 0, 0, 0, 2, 0, 0, 0, 0, 2, 0, 1, 2, 3, 1};
  int i;

  CHECK(strcmp(data->asset.version, "2.0") == 0);
  CHECK(strcmp(data->asset.generator, "cabin-ports test") == 0);
  CHECK(data->buffers_count == 1 && data->buffers[0].size == 44 && data->buffers[0].data != NULL);
  CHECK(data->buffer_views_count == 2 && data->buffer_views[1].offset == 36 &&
        data->buffer_views[1].size == 6);
  CHECK(data->nodes_count == 2 && data->scenes_count == 1 && data->scene == &data->scenes[0]);

  root = &data->nodes[0];
  child = &data->nodes[1];
  CHECK(strcmp(root->name, "root") == 0 && strcmp(child->name, "child") == 0);
  CHECK(data->scene->nodes_count == 1 && data->scene->nodes[0] == root);
  CHECK(root->parent == NULL && root->children_count == 1 && root->children[0] == child);
  CHECK(child->parent == root && child->mesh == &data->meshes[0]);
  CHECK(root->has_translation && !root->has_scale && child->has_scale && !child->has_translation);

  /* Scale by 2, then translate by (1,2,3); column-major. */
  cgltf_node_transform_world(child, world);
  for (i = 0; i < 16; ++i) CHECK(approx(world[i], expected_world[i]));

  CHECK(strcmp(child->mesh->name, "triangle") == 0 && child->mesh->primitives_count == 1);
  prim = &child->mesh->primitives[0];
  CHECK(prim->type == cgltf_primitive_type_triangles && prim->attributes_count == 1);
  positions = cgltf_find_accessor(prim, cgltf_attribute_type_position, 0);
  CHECK(positions != NULL && positions == prim->attributes[0].data);
  if (positions == NULL || prim->indices == NULL) return;
  CHECK(positions->type == cgltf_type_vec3 &&
        positions->component_type == cgltf_component_type_r_32f && positions->count == 3 &&
        positions->stride == 12);
  CHECK(positions->has_min && positions->has_max && approx(positions->max[0], 1) &&
        approx(positions->max[1], 2) && approx(positions->min[2], 0));

  CHECK(cgltf_accessor_read_float(positions, 2, vertex, 3));
  CHECK(approx(vertex[0], 0) && approx(vertex[1], 2) && approx(vertex[2], 0));
  /* The third vertex in world space: (0*2+1, 2*2+2, 0*2+3). */
  CHECK(approx(world[0] * vertex[0] + world[4] * vertex[1] + world[8] * vertex[2] + world[12], 1) &&
        approx(world[1] * vertex[0] + world[5] * vertex[1] + world[9] * vertex[2] + world[13], 6) &&
        approx(world[2] * vertex[0] + world[6] * vertex[1] + world[10] * vertex[2] + world[14], 3));

  CHECK(cgltf_accessor_unpack_floats(positions, NULL, 0) == 9);
  CHECK(cgltf_accessor_unpack_floats(positions, unpacked, 9) == 9);
  CHECK(approx(unpacked[3], 1) && approx(unpacked[7], 2) && approx(unpacked[8], 0));

  CHECK(prim->indices->component_type == cgltf_component_type_r_16u && prim->indices->count == 3);
  CHECK(cgltf_accessor_unpack_indices(prim->indices, indices, sizeof indices[0], 3) == 3);
  CHECK(indices[0] == 0 && indices[1] == 1 && indices[2] == 2);
  CHECK(cgltf_accessor_read_index(prim->indices, 2) == 2);
}

static void rejects(void) {
  static const char truncated[] = "{\"asset\": {\"version\": \"2.0\"";
  static const char legacy[] = "{\"asset\": {\"version\": \"1.0\"}}";
  /* The buffer view runs four bytes past the end of its buffer. */
  static const char overrun[] =
      "{\"asset\": {\"version\": \"2.0\"}, \"buffers\": [{\"byteLength\": 4}],"
      " \"bufferViews\": [{\"buffer\": 0, \"byteLength\": 8}]}";
  cgltf_options options;
  cgltf_data* data = NULL;
  memset(&options, 0, sizeof options);

  CHECK(cgltf_parse(&options, "{}", 2, &data) == cgltf_result_data_too_short);
  CHECK(cgltf_parse(&options, truncated, sizeof truncated - 1, &data) ==
        cgltf_result_invalid_json);
  CHECK(cgltf_parse(&options, legacy, sizeof legacy - 1, &data) == cgltf_result_legacy_gltf);

  options.type = cgltf_file_type_glb;
  CHECK(cgltf_parse(&options, legacy, sizeof legacy - 1, &data) == cgltf_result_unknown_format);
  options.type = cgltf_file_type_invalid;

  CHECK(cgltf_parse(&options, overrun, sizeof overrun - 1, &data) == cgltf_result_success);
  if (data != NULL) {
    CHECK(cgltf_validate(data) == cgltf_result_data_too_short);
    cgltf_free(data);
  }
}

static void round_trip(const cgltf_data* data) {
  cgltf_options options;
  cgltf_size size;
  char* json;
  cgltf_data* copy;
  memset(&options, 0, sizeof options);

  size = cgltf_write(&options, NULL, 0, data);
  CHECK(size > 1);
  json = (char*)malloc(size);
  if (json == NULL) return;
  CHECK(cgltf_write(&options, json, size, data) == size);
  CHECK(json[size - 1] == '\0' && strlen(json) == size - 1);
  copy = load(json, size - 1);
  if (copy != NULL) {
    inspect(copy);
    cgltf_free(copy);
  }
  free(json);
}

int main(void) {
  cgltf_data* data = load(document, sizeof document - 1);
  if (data != NULL) {
    inspect(data);
    /* From "target"; cgltf_write omits it, so only the original has these. */
    CHECK(data->buffer_views[0].type == cgltf_buffer_view_type_vertices &&
          data->buffer_views[1].type == cgltf_buffer_view_type_indices);
    round_trip(data);
    cgltf_free(data);
  }
  rejects();
  printf("cgltf (C): %s\n", failures == 0 ? "ok" : "FAILED");
  return failures == 0 ? 0 : 1;
}
