/* C++ consumer: an atom object written to Turtle with sratom_to_turtle and read
 * back with sratom_from_turtle. */
#include <sratom/sratom.h>

#include <lv2/atom/atom.h>
#include <lv2/atom/forge.h>
#include <lv2/atom/util.h>
#include <lv2/urid/urid.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

static void check(int ok, const char* what, int line) {
  if (!ok) {
    fprintf(stderr, "main.cc:%d: check failed: %s\n", line, what);
    ++failures;
  }
}
#define CHECK(cond) check((cond), #cond, __LINE__)

/* The host's URID table; every URI here is a string literal. */
typedef struct {
  const char* uris[128];
  uint32_t n;
} UriTable;

static LV2_URID map_uri(LV2_URID_Map_Handle handle, const char* uri) {
  UriTable* table = static_cast<UriTable*>(handle);
  for (uint32_t i = 0; i < table->n; ++i) {
    if (!strcmp(table->uris[i], uri)) return i + 1;
  }
  if (table->n == 128) return 0;
  table->uris[table->n] = uri;
  return ++table->n;
}

static const char* unmap_uri(LV2_URID_Unmap_Handle handle, LV2_URID urid) {
  UriTable* table = static_cast<UriTable*>(handle);
  return (urid >= 1 && urid <= table->n) ? table->uris[urid - 1] : NULL;
}

#define EX "http://example.org/"

int main(void) {
  UriTable table = {{}, 0};
  LV2_URID_Map map = {&table, map_uri};
  LV2_URID_Unmap unmap = {&table, unmap_uri};
  const LV2_URID thing = map.map(map.handle, EX "Thing");
  const LV2_URID k_int = map.map(map.handle, EX "count");
  const LV2_URID k_str = map.map(map.handle, EX "label");

  /* Forge an object holding an int and a string into a resizing buffer. */
  SerdChunk chunk = {NULL, 0};
  LV2_Atom_Forge forge;
  lv2_atom_forge_init(&forge, &map);
  lv2_atom_forge_set_sink(&forge, sratom_forge_sink, sratom_forge_deref, &chunk);
  LV2_Atom_Forge_Frame frame;
  const LV2_Atom_Forge_Ref ref = lv2_atom_forge_object(&forge, &frame, 0, thing);
  CHECK(ref != 0);
  lv2_atom_forge_key(&forge, k_int);
  lv2_atom_forge_int(&forge, 42);
  lv2_atom_forge_key(&forge, k_str);
  lv2_atom_forge_string(&forge, "hello", 5);
  lv2_atom_forge_pop(&forge, &frame);
  const LV2_Atom* atom = sratom_forge_deref(&chunk, ref);
  CHECK(atom != NULL && atom->type == forge.Object);

  Sratom* sratom = sratom_new(&map);
  const SerdNode subj = serd_node_from_string(SERD_URI, (const uint8_t*)(EX "s"));
  const SerdNode pred = serd_node_from_string(SERD_URI, (const uint8_t*)(EX "p"));
  char* ttl = sratom_to_turtle(sratom, &unmap, EX, &subj, &pred, atom->type, atom->size,
                               LV2_ATOM_BODY_CONST(atom));
  CHECK(ttl != NULL);
  if (ttl != NULL) {
    CHECK(strstr(ttl, "\"hello\"") != NULL);
    CHECK(strstr(ttl, EX "Thing") != NULL);

    LV2_Atom* back = sratom_from_turtle(sratom, EX, &subj, &pred, ttl);
    CHECK(back != NULL && back->type == forge.Object);
    if (back != NULL && back->type == forge.Object) {
      const LV2_Atom_Object* obj = (const LV2_Atom_Object*)back;
      const LV2_Atom* count = NULL;
      const LV2_Atom* label = NULL;
      lv2_atom_object_get(obj, k_int, &count, k_str, &label, 0);
      CHECK(obj->body.otype == thing);
      CHECK(count != NULL && count->type == forge.Int && ((const LV2_Atom_Int*)count)->body == 42);
      CHECK(label != NULL && label->type == forge.String &&
            strcmp((const char*)LV2_ATOM_BODY_CONST(label), "hello") == 0);
    }
    free(back);
  }
  free(ttl);
  sratom_free(sratom);
  serd_free(serd_chunk_sink_finish(&chunk));

  if (failures != 0) {
    fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  puts("sratom C++ consumer test passed");
  return 0;
}
