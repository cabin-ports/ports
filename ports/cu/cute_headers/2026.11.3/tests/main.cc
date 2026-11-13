// Consumer test for cabin-ports/cute_headers from C++: the seven headers
// without outside dependencies compile together, a Tiled JSON map is parsed
// from memory and checked, and a collision query and a PNG round trip run as
// in main.c.
#define CUTE_ASEPRITE_IMPLEMENTATION
#include <cute_aseprite.h>
#define CUTE_C2_IMPLEMENTATION
#include <cute_c2.h>
#define CUTE_NET_IMPLEMENTATION
#include <cute_net.h>
#define CUTE_PNG_IMPLEMENTATION
#include <cute_png.h>
#define SPRITEBATCH_IMPLEMENTATION
#include <cute_spritebatch.h>
#ifdef _WIN32
#define CUTE_SYNC_WINDOWS
#else
#define CUTE_SYNC_POSIX
#endif
#define CUTE_SYNC_IMPLEMENTATION
#include <cute_sync.h>
#define CUTE_TILED_IMPLEMENTATION
#include <cute_tiled.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

static int failures = 0;

static void check(bool pass, const char *what) {
  if (!pass) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

// Keys as in upstream's test_cute_tiled/test_cute_tiled.json.
static const char map_json[] =
    "{ \"compressionlevel\":-1, \"height\":2, \"infinite\":false,\n"
    "  \"layers\":[ { \"data\":[1, 2, 3, 4, 0, 6], \"height\":2, \"id\":1, \"name\":\"ground\",\n"
    "                 \"opacity\":0.5, \"type\":\"tilelayer\", \"visible\":true, \"width\":3, \"x\":0, \"y\":0 } ],\n"
    "  \"nextlayerid\":2, \"nextobjectid\":1, \"orientation\":\"orthogonal\", \"renderorder\":\"right-down\",\n"
    "  \"tiledversion\":\"1.10.2\", \"tileheight\":16,\n"
    "  \"tilesets\":[ { \"columns\":8, \"firstgid\":1, \"image\":\"tiles.png\", \"imageheight\":64,\n"
    "                   \"imagewidth\":128, \"margin\":0, \"name\":\"tiles\", \"spacing\":0, \"tilecount\":32,\n"
    "                   \"tileheight\":16, \"tilewidth\":16 } ],\n"
    "  \"tilewidth\":16, \"type\":\"map\", \"version\":\"1.10\", \"width\":3 }\n";

static void test_tiled() {
  cute_tiled_map_t *map = cute_tiled_load_map_from_memory(map_json, (int)sizeof(map_json) - 1, NULL);
  check(map != NULL, "the map parses");
  if (!map) return;
  check(map->width == 3 && map->height == 2 && map->tilewidth == 16 && map->tileheight == 16, "the map's grid");
  check(std::strcmp(map->orientation.ptr, "orthogonal") == 0, "the map's orientation");

  cute_tiled_layer_t *layer = map->layers;
  check(layer != NULL && layer->next == NULL, "the map has one layer");
  if (layer) {
    static const int expected[6] = {1, 2, 3, 4, 0, 6};
    check(std::strcmp(layer->name.ptr, "ground") == 0 && std::strcmp(layer->type.ptr, "tilelayer") == 0,
          "the layer's name and type");
    check(layer->data_count == 6 && std::memcmp(layer->data, expected, sizeof(expected)) == 0,
          "the layer's tile GIDs");
    check(layer->opacity == 0.5f && layer->visible, "the layer's opacity and visibility");
  }

  cute_tiled_tileset_t *tileset = map->tilesets;
  check(tileset != NULL && tileset->firstgid == 1 && tileset->columns == 8 && tileset->tilecount == 32 &&
            std::strcmp(tileset->name.ptr, "tiles") == 0 && std::strcmp(tileset->image.ptr, "tiles.png") == 0,
        "the tileset");
  cute_tiled_free_map(map);
}

static void test_c2() {
  c2Circle a = {c2V(0, 0), 1};
  c2Circle b = {c2V(1.5f, 0), 1};
  c2Circle distant = {c2V(3, 0), 1};
  check(c2CircletoCircle(a, b) && !c2CircletoCircle(a, distant), "circles collide only when they overlap");
}

static void test_png() {
  cp_image_t img = cp_load_blank(2, 2);
  for (int i = 0; i < 4; ++i) {
    cp_pixel_t p = {(uint8_t)(i * 60), (uint8_t)(255 - i * 60), (uint8_t)i, 0xff};
    img.pix[i] = p;
  }
  cp_saved_png_t saved = cp_save_png_to_memory(&img);
  cp_image_t back = cp_load_png_mem(saved.data, saved.size);
  check(back.pix != NULL && back.w == 2 && back.h == 2 &&
            std::memcmp(back.pix, img.pix, sizeof(cp_pixel_t) * 4) == 0,
        "a saved PNG loads back with the same pixels");
  std::free(saved.data);
  cp_free_png(&img);
  cp_free_png(&back);
}

int main() {
  test_tiled();
  test_c2();
  test_png();
  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("cute_headers 2026.11.3 C++ ok");
  return 0;
}
