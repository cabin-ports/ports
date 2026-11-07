/* Consumer test: version API, event parsing, document loading, emitting a
   built document to a buffer and reading it back, Unicode escaping, and
   error reporting with marks. */
#include <yaml.h>

#include <stdio.h>
#include <string.h>

static int failures = 0;

static void check(int ok, const char *what, int line)
{
  if (!ok) {
    fprintf(stderr, "main.c:%d: check failed: %s\n", line, what);
    ++failures;
  }
}
#define CHECK(cond) check((cond) ? 1 : 0, #cond, __LINE__)

static const char *scalar(yaml_node_t *node)
{
  if (node == NULL || node->type != YAML_SCALAR_NODE) return "";
  return (const char *)node->data.scalar.value;
}

/* Value of KEY in the mapping NODE, or NULL. */
static yaml_node_t *lookup(yaml_document_t *doc, yaml_node_t *node,
                           const char *key)
{
  yaml_node_pair_t *pair;
  if (node == NULL || node->type != YAML_MAPPING_NODE) return NULL;
  for (pair = node->data.mapping.pairs.start;
       pair < node->data.mapping.pairs.top; ++pair) {
    if (strcmp(scalar(yaml_document_get_node(doc, pair->key)), key) == 0)
      return yaml_document_get_node(doc, pair->value);
  }
  return NULL;
}

static void test_version(void)
{
  int major = -1, minor = -1, patch = -1;
  yaml_get_version(&major, &minor, &patch);
  CHECK(major == 0 && minor == 2 && patch == 5);
  CHECK(strcmp(yaml_get_version_string(), "0.2.5") == 0);
}

static void test_events(void)
{
  static const char input[] = "key: [a, b]\nn: 42\n";
  static const char *expected[] = {"key", "a", "b", "n", "42"};
  yaml_parser_t parser;
  yaml_event_t event;
  int scalars = 0, sequences = 0, done = 0;
  CHECK(yaml_parser_initialize(&parser));
  yaml_parser_set_input_string(&parser, (const unsigned char *)input,
                               sizeof input - 1);
  while (!done) {
    if (!yaml_parser_parse(&parser, &event)) {
      CHECK(!"yaml_parser_parse failed");
      break;
    }
    if (event.type == YAML_SCALAR_EVENT) {
      if (scalars < 5)
        CHECK(strcmp((const char *)event.data.scalar.value,
                     expected[scalars]) == 0);
      ++scalars;
    } else if (event.type == YAML_SEQUENCE_START_EVENT) {
      CHECK(event.data.sequence_start.style == YAML_FLOW_SEQUENCE_STYLE);
      ++sequences;
    }
    done = event.type == YAML_STREAM_END_EVENT;
    yaml_event_delete(&event);
  }
  CHECK(scalars == 5);
  CHECK(sequences == 1);
  yaml_parser_delete(&parser);
}

static int refuse_write(void *data, unsigned char *buffer, size_t size)
{
  (void)buffer;
  (void)size;
  ++*(int *)data;
  return 0;
}

/* Builds {name: libyaml, list: [1, 2]} and emits it into OUT. */
static size_t emit_document(unsigned char *out, size_t capacity, int unicode,
                            const char *name)
{
  yaml_document_t doc;
  yaml_emitter_t emitter;
  size_t written = 0;
  int root, list;
  CHECK(yaml_document_initialize(&doc, NULL, NULL, NULL, 1, 1));
  root = yaml_document_add_mapping(&doc, NULL, YAML_BLOCK_MAPPING_STYLE);
  list = yaml_document_add_sequence(&doc, NULL, YAML_FLOW_SEQUENCE_STYLE);
  CHECK(root == 1 && list == 2);
  CHECK(yaml_document_append_mapping_pair(
      &doc, root,
      yaml_document_add_scalar(&doc, NULL, (yaml_char_t *)"name", -1,
                               YAML_PLAIN_SCALAR_STYLE),
      yaml_document_add_scalar(&doc, NULL, (yaml_char_t *)name, -1,
                               YAML_ANY_SCALAR_STYLE)));
  CHECK(yaml_document_append_mapping_pair(
      &doc, root,
      yaml_document_add_scalar(&doc, NULL, (yaml_char_t *)"list", -1,
                               YAML_PLAIN_SCALAR_STYLE),
      list));
  CHECK(yaml_document_append_sequence_item(
      &doc, list,
      yaml_document_add_scalar(&doc, NULL, (yaml_char_t *)"1", -1,
                               YAML_PLAIN_SCALAR_STYLE)));
  CHECK(yaml_document_append_sequence_item(
      &doc, list,
      yaml_document_add_scalar(&doc, NULL, (yaml_char_t *)"2", -1,
                               YAML_PLAIN_SCALAR_STYLE)));
  CHECK(yaml_emitter_initialize(&emitter));
  yaml_emitter_set_output_string(&emitter, out, capacity - 1, &written);
  yaml_emitter_set_unicode(&emitter, unicode);
  CHECK(yaml_emitter_open(&emitter));
  CHECK(yaml_emitter_dump(&emitter, &doc)); /* dump also deletes doc */
  CHECK(yaml_emitter_close(&emitter));
  yaml_emitter_delete(&emitter);
  out[written] = '\0';
  return written;
}

static void test_emit_and_load(void)
{
  unsigned char out[256];
  yaml_parser_t parser;
  yaml_document_t doc;
  yaml_node_t *root, *list;
  size_t size = emit_document(out, sizeof out, 0, "libyaml");
  CHECK(strcmp((const char *)out, "name: libyaml\nlist: [1, 2]\n") == 0);

  CHECK(yaml_parser_initialize(&parser));
  yaml_parser_set_input_string(&parser, out, size);
  CHECK(yaml_parser_load(&parser, &doc));
  root = yaml_document_get_root_node(&doc);
  CHECK(root != NULL && root->type == YAML_MAPPING_NODE);
  CHECK(strcmp(scalar(lookup(&doc, root, "name")), "libyaml") == 0);
  list = lookup(&doc, root, "list");
  CHECK(list != NULL && list->type == YAML_SEQUENCE_NODE);
  if (list != NULL && list->type == YAML_SEQUENCE_NODE) {
    CHECK(list->data.sequence.items.top - list->data.sequence.items.start ==
          2);
    CHECK(strcmp(scalar(yaml_document_get_node(
                     &doc, list->data.sequence.items.start[1])),
                 "2") == 0);
  }
  yaml_document_delete(&doc);
  yaml_parser_delete(&parser);
}

static void test_unicode(void)
{
  /* "caf" then U+00E9 in UTF-8. */
  unsigned char out[256];
  emit_document(out, sizeof out, 1, "caf\xc3\xa9");
  CHECK(strcmp((const char *)out, "name: caf\xc3\xa9\nlist: [1, 2]\n") == 0);
  /* Without unicode output the emitter must escape it. */
  emit_document(out, sizeof out, 0, "caf\xc3\xa9");
  CHECK(strcmp((const char *)out, "name: \"caf\\xE9\"\nlist: [1, 2]\n") == 0);
}

static void test_errors(void)
{
  static const char input[] = "a: 1\nb: [x, y\n";
  yaml_parser_t parser;
  yaml_document_t doc;
  yaml_emitter_t emitter;
  int calls = 0;
  int ok;
  CHECK(yaml_parser_initialize(&parser));
  yaml_parser_set_input_string(&parser, (const unsigned char *)input,
                               sizeof input - 1);
  CHECK(!yaml_parser_load(&parser, &doc));
  CHECK(parser.error == YAML_PARSER_ERROR);
  CHECK(parser.problem != NULL &&
        strstr(parser.problem, "did not find expected") != NULL);
  CHECK(parser.problem_mark.line == 2);
  yaml_parser_delete(&parser);

  /* An output handler that refuses data is reported as a writer error. */
  CHECK(yaml_document_initialize(&doc, NULL, NULL, NULL, 1, 1));
  yaml_document_add_scalar(&doc, NULL, (yaml_char_t *)"x", -1,
                           YAML_PLAIN_SCALAR_STYLE);
  CHECK(yaml_emitter_initialize(&emitter));
  yaml_emitter_set_output(&emitter, refuse_write, &calls);
  ok = yaml_emitter_dump(&emitter, &doc) && yaml_emitter_close(&emitter);
  CHECK(!ok);
  CHECK(emitter.error == YAML_WRITER_ERROR);
  CHECK(calls > 0);
  yaml_emitter_delete(&emitter);
}

int main(void)
{
  test_version();
  test_events();
  test_emit_and_load();
  test_unicode();
  test_errors();
  if (failures != 0) {
    fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  puts("libyaml consumer test passed");
  return 0;
}
