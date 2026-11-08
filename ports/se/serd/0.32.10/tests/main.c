/* C consumer: Turtle read into user sinks and through the writer as
 * N-Triples, syntax errors in lax and strict reading, and URI resolution. */
#include <serd/serd.h>

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

static int node_is(const SerdNode* node, SerdType type, const char* str) {
  return node && node->type == type && node->buf && strcmp((const char*)node->buf, str) == 0;
}

static const char turtle[] =
    "@base <http://example.org/> .\n"
    "@prefix ex: <http://example.org/ns#> .\n"
    "<doc> a ex:Document ;\n"
    "  ex:title \"Caf\xC3\xA9\"@fr , \"Serd\" ;\n"
    "  ex:pages 42 ;\n"
    "  ex:author [ ex:name \"Ann\" ] .\n";

static const char ntriples[] =
    "<http://example.org/doc> <http://www.w3.org/1999/02/22-rdf-syntax-ns#type> "
    "<http://example.org/ns#Document> .\n"
    "<http://example.org/doc> <http://example.org/ns#title> \"Caf\\u00E9\"@fr .\n"
    "<http://example.org/doc> <http://example.org/ns#title> \"Serd\" .\n"
    "<http://example.org/doc> <http://example.org/ns#pages> "
    "\"42\"^^<http://www.w3.org/2001/XMLSchema#integer> .\n"
    "<http://example.org/doc> <http://example.org/ns#author> _:b1 .\n"
    "_:b1 <http://example.org/ns#name> \"Ann\" .\n";

typedef struct {
  SerdEnv* env;
  int bases;
  int prefixes;
  int statements;
  int ends;
  int document_type;
  int french_title;
  int integer_pages;
  int anon_author;
  int anon_name;
} Seen;

static SerdStatus on_base(void* handle, const SerdNode* uri) {
  Seen* seen = (Seen*)handle;
  ++seen->bases;
  CHECK(node_is(uri, SERD_URI, "http://example.org/"));
  return serd_env_set_base_uri(seen->env, uri);
}

static SerdStatus on_prefix(void* handle, const SerdNode* name, const SerdNode* uri) {
  Seen* seen = (Seen*)handle;
  ++seen->prefixes;
  CHECK(node_is(name, SERD_LITERAL, "ex"));
  CHECK(node_is(uri, SERD_URI, "http://example.org/ns#"));
  return serd_env_set_prefix(seen->env, name, uri);
}

static SerdStatus on_statement(void* handle, SerdStatementFlags flags, const SerdNode* graph,
                               const SerdNode* subject, const SerdNode* predicate,
                               const SerdNode* object, const SerdNode* datatype,
                               const SerdNode* lang) {
  Seen* seen = (Seen*)handle;
  ++seen->statements;
  CHECK(graph == NULL || graph->type == SERD_NOTHING);

  /* The reader hands on relative URIs and CURIEs unresolved. */
  if (node_is(predicate, SERD_URI, "http://www.w3.org/1999/02/22-rdf-syntax-ns#type")) {
    SerdNode expanded = serd_env_expand_node(seen->env, object);
    seen->document_type = node_is(subject, SERD_URI, "doc") &&
                          node_is(object, SERD_CURIE, "ex:Document") &&
                          node_is(&expanded, SERD_URI, "http://example.org/ns#Document");
    serd_node_free(&expanded);
  } else if (node_is(object, SERD_LITERAL, "Caf\xC3\xA9")) {
    seen->french_title = object->n_bytes == 5 && node_is(lang, SERD_LITERAL, "fr") &&
                         !(datatype && datatype->buf);
  } else if (node_is(object, SERD_LITERAL, "42")) {
    seen->integer_pages = node_is(datatype, SERD_URI, "http://www.w3.org/2001/XMLSchema#integer");
  } else if (node_is(predicate, SERD_CURIE, "ex:author")) {
    seen->anon_author = node_is(object, SERD_BLANK, "b1") && (flags & SERD_ANON_O_BEGIN);
  } else if (node_is(predicate, SERD_CURIE, "ex:name")) {
    seen->anon_name = node_is(subject, SERD_BLANK, "b1") && (flags & SERD_ANON_CONT) &&
                      node_is(object, SERD_LITERAL, "Ann");
  }
  return SERD_SUCCESS;
}

static SerdStatus on_end(void* handle, const SerdNode* node) {
  Seen* seen = (Seen*)handle;
  ++seen->ends;
  CHECK(node_is(node, SERD_BLANK, "b1"));
  return SERD_SUCCESS;
}

typedef struct {
  int count;
  SerdStatus status;
  unsigned line;
} Errors;

static SerdStatus on_error(void* handle, const SerdError* error) {
  Errors* errors = (Errors*)handle;
  ++errors->count;
  errors->status = error->status;
  errors->line = error->line;
  return SERD_SUCCESS;
}

static SerdStatus count_statement(void* handle, SerdStatementFlags flags, const SerdNode* graph,
                                  const SerdNode* subject, const SerdNode* predicate,
                                  const SerdNode* object, const SerdNode* datatype,
                                  const SerdNode* lang) {
  (void)flags, (void)graph, (void)subject, (void)predicate, (void)datatype, (void)lang;
  CHECK(node_is(object, SERD_LITERAL, "ok"));
  ++*(int*)handle;
  return SERD_SUCCESS;
}

/* The writer's functions take a SerdWriter*, so they are called through
 * sinks of the exact types the reader expects. */
static SerdStatus write_base(void* handle, const SerdNode* uri) {
  return serd_writer_set_base_uri((SerdWriter*)handle, uri);
}

static SerdStatus write_prefix(void* handle, const SerdNode* name, const SerdNode* uri) {
  return serd_writer_set_prefix((SerdWriter*)handle, name, uri);
}

static SerdStatus write_statement(void* handle, SerdStatementFlags flags, const SerdNode* graph,
                                  const SerdNode* subject, const SerdNode* predicate,
                                  const SerdNode* object, const SerdNode* datatype,
                                  const SerdNode* lang) {
  return serd_writer_write_statement((SerdWriter*)handle, flags, graph, subject, predicate,
                                     object, datatype, lang);
}

static SerdStatus write_end(void* handle, const SerdNode* node) {
  return serd_writer_end_anon((SerdWriter*)handle, node);
}

/* Line 1's string runs into the line end; line 2 is a whole statement. */
static void read_broken(int strict) {
  static const char broken[] =
      "<http://example.org/s> <http://example.org/p> \"broken\n"
      "<http://example.org/s> <http://example.org/p> \"ok\" .\n";
  int statements = 0;
  Errors errors = {0, SERD_SUCCESS, 0};
  SerdReader* reader = serd_reader_new(SERD_TURTLE, &statements, NULL, NULL, NULL,
                                       count_statement, NULL);
  serd_reader_set_strict(reader, strict != 0);
  serd_reader_set_error_sink(reader, on_error, &errors);
  const SerdStatus st = serd_reader_read_string(reader, (const uint8_t*)broken);
  serd_reader_free(reader);

  /* Lax reading reports the error and resumes on the next line. */
  CHECK(st == (strict ? SERD_ERR_BAD_SYNTAX : SERD_SUCCESS));
  CHECK(errors.count == 1);
  CHECK(errors.status == SERD_ERR_BAD_SYNTAX);
  CHECK(errors.line == 1);
  CHECK(statements == (strict ? 0 : 1));
}

static void check_resolve(const SerdURI* base, const char* reference, const char* expected) {
  SerdNode node = serd_node_new_uri_from_string((const uint8_t*)reference, base, NULL);
  CHECK(node_is(&node, SERD_URI, expected));
  serd_node_free(&node);
}

int main(void) {
  {
    Seen seen;
    memset(&seen, 0, sizeof seen);
    seen.env = serd_env_new(NULL);
    SerdReader* reader =
        serd_reader_new(SERD_TURTLE, &seen, NULL, on_base, on_prefix, on_statement, on_end);
    CHECK(serd_reader_read_string(reader, (const uint8_t*)turtle) == SERD_SUCCESS);
    serd_reader_free(reader);
    serd_env_free(seen.env);

    CHECK(seen.bases == 1);
    CHECK(seen.prefixes == 1);
    CHECK(seen.statements == 6);
    CHECK(seen.ends == 1);
    CHECK(seen.document_type);
    CHECK(seen.french_title);
    CHECK(seen.integer_pages);
    CHECK(seen.anon_author);
    CHECK(seen.anon_name);
  }

  {
    SerdEnv* env = serd_env_new(NULL);
    SerdChunk chunk = {NULL, 0};
    SerdWriter* writer = serd_writer_new(SERD_NTRIPLES,
                                         (SerdStyle)(SERD_STYLE_ASCII | SERD_STYLE_RESOLVED), env,
                                         NULL, serd_chunk_sink, &chunk);
    SerdReader* reader = serd_reader_new(SERD_TURTLE, writer, NULL, write_base, write_prefix,
                                         write_statement, write_end);
    CHECK(serd_reader_read_string(reader, (const uint8_t*)turtle) == SERD_SUCCESS);
    CHECK(serd_writer_finish(writer) == SERD_SUCCESS);
    serd_reader_free(reader);
    serd_writer_free(writer);
    serd_env_free(env);

    uint8_t* out = serd_chunk_sink_finish(&chunk);
    CHECK(out != NULL && strcmp((const char*)out, ntriples) == 0);
    if (out != NULL && strcmp((const char*)out, ntriples) != 0) {
      fprintf(stderr, "N-Triples output:\n%s", (const char*)out);
    }
    serd_free(out);
  }

  read_broken(0);
  read_broken(1);

  {
    const SerdNode node =
        serd_node_from_string(SERD_LITERAL, (const uint8_t*)"Caf\xC3\xA9 \"q\"\n");
    CHECK(node.n_bytes == 10 && node.n_chars == 9);
    CHECK(node.flags == (SERD_HAS_QUOTE | SERD_HAS_NEWLINE));
  }

  {
    SerdURI base;
    SerdNode base_node =
        serd_node_new_uri_from_string((const uint8_t*)"http://example.org/a/b/c", NULL, &base);
    check_resolve(&base, "../d?q#f", "http://example.org/a/d?q#f");
    check_resolve(&base, "e", "http://example.org/a/b/e");
    check_resolve(&base, "//other.org/x", "http://other.org/x");
    serd_node_free(&base_node);

    SerdURI uri;
    SerdURI dir;
    SerdNode uri_node =
        serd_node_new_uri_from_string((const uint8_t*)"http://example.org/a/b", NULL, &uri);
    SerdNode dir_node =
        serd_node_new_uri_from_string((const uint8_t*)"http://example.org/a/b/", NULL, &dir);
    SerdNode relative = serd_node_new_relative_uri(&uri, &dir, NULL, NULL);
    CHECK(node_is(&relative, SERD_URI, "../b"));
    serd_node_free(&relative);
    serd_node_free(&dir_node);
    serd_node_free(&uri_node);
  }

  if (failures != 0) {
    fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  puts("serd C consumer test passed");
  return 0;
}
