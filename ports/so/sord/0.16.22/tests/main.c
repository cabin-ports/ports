/* C consumer: nodes, quads, pattern search and counting in a model, removal,
 * and loading Turtle then writing it back as N-Triples. */
#include <sord/sord.h>

#include <stdio.h>
#include <string.h>

static int failures = 0;

static void check(int ok, const char* what, int line) {
  if (!ok) {
    fprintf(stderr, "main.c:%d: check failed: %s\n", line, what);
    ++failures;
  }
}
#define CHECK(cond) check((cond), #cond, __LINE__)

#define U(s) ((const uint8_t*)(s))

static const char turtle[] =
    "@prefix ex: <http://example.org/> .\n"
    "ex:a ex:p \"one\" , \"two\"@en .\n"
    "ex:b ex:p ex:a .\n";

int main(void) {
  SordWorld* world = sord_world_new();
  SordModel* model = sord_new(world, SORD_SPO | SORD_OPS, false);

  SordNode* a = sord_new_uri(world, U("http://example.org/a"));
  SordNode* b = sord_new_uri(world, U("http://example.org/b"));
  SordNode* p = sord_new_uri(world, U("http://example.org/p"));
  SordNode* hello = sord_new_literal(world, NULL, U("hello"), NULL);
  SordNode* bonjour = sord_new_literal(world, NULL, U("bonjour"), "fr");

  CHECK(sord_node_get_type(a) == SORD_URI);
  CHECK(strcmp((const char*)sord_node_get_string(hello), "hello") == 0);
  CHECK(strcmp(sord_node_get_language(bonjour), "fr") == 0);
  SordNode* a2 = sord_new_uri(world, U("http://example.org/a"));
  CHECK(a2 == a && sord_node_equals(a, a2));
  sord_node_free(world, a2);
  CHECK(!sord_node_equals(a, b));

  const SordQuad q1 = {a, p, hello, NULL};
  const SordQuad q2 = {a, p, bonjour, NULL};
  const SordQuad q3 = {b, p, a, NULL};
  CHECK(sord_add(model, q1));
  CHECK(sord_add(model, q2));
  CHECK(sord_add(model, q3));
  CHECK(!sord_add(model, q1));
  CHECK(sord_num_quads(model) == 3);

  CHECK(sord_ask(model, a, p, hello, NULL));
  CHECK(!sord_ask(model, b, p, hello, NULL));
  CHECK(sord_count(model, a, p, NULL, NULL) == 2);
  CHECK(sord_count(model, NULL, p, NULL, NULL) == 3);
  CHECK(sord_count(model, NULL, NULL, a, NULL) == 1);

  SordNode* got = sord_get(model, b, p, NULL, NULL);
  CHECK(got == a);
  sord_node_free(world, got);

  const SordQuad pattern = {NULL, p, a, NULL};
  SordIter* iter = sord_find(model, pattern);
  CHECK(iter != NULL && !sord_iter_end(iter));
  CHECK(iter && sord_iter_get_node(iter, SORD_SUBJECT) == b);
  /* sord_iter_next returns true once it has reached the end. */
  CHECK(iter && sord_iter_next(iter) && sord_iter_end(iter));
  sord_iter_free(iter);

  sord_remove(model, q1);
  CHECK(sord_num_quads(model) == 2);
  CHECK(!sord_contains(model, q1));
  CHECK(sord_contains(model, q2));

  sord_node_free(world, bonjour);
  sord_node_free(world, hello);
  sord_node_free(world, p);
  sord_node_free(world, b);
  sord_node_free(world, a);
  sord_free(model);

  /* Load Turtle into a fresh model, then write the model as N-Triples. */
  SordModel* loaded = sord_new(world, SORD_SPO | SORD_OPS, false);
  SerdEnv* env = serd_env_new(NULL);
  SerdReader* reader = sord_new_reader(loaded, env, SERD_TURTLE, NULL);
  CHECK(serd_reader_read_string(reader, U(turtle)) == SERD_SUCCESS);
  serd_reader_free(reader);
  CHECK(sord_num_quads(loaded) == 3);

  SerdChunk chunk = {NULL, 0};
  SerdWriter* writer = serd_writer_new(SERD_NTRIPLES, (SerdStyle)0, env, NULL, serd_chunk_sink,
                                       &chunk);
  CHECK(sord_write(loaded, writer, NULL));
  serd_writer_free(writer);
  uint8_t* out = serd_chunk_sink_finish(&chunk);
  CHECK(out != NULL);
  if (out != NULL) {
    const char* text = (const char*)out;
    CHECK(strstr(text, "<http://example.org/a> <http://example.org/p> \"one\" .\n") != NULL);
    CHECK(strstr(text, "<http://example.org/a> <http://example.org/p> \"two\"@en .\n") != NULL);
    CHECK(strstr(text, "<http://example.org/b> <http://example.org/p> <http://example.org/a> .\n") !=
          NULL);
  }
  serd_free(out);
  serd_env_free(env);
  sord_free(loaded);
  sord_world_free(world);

  if (failures != 0) {
    fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  puts("sord C consumer test passed");
  return 0;
}
