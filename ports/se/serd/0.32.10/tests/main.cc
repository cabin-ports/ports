// serd.h from C++ (its declarations are extern "C"): Turtle read in small
// pages from memory into std::string triples, written back as abbreviated,
// paged Turtle, and read again to the same triples.
#include <serd/serd.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
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

const char turtle[] =
    "@prefix ex: <http://example.org/ns#> .\n"
    "@prefix xsd: <http://www.w3.org/2001/XMLSchema#> .\n"
    "ex:book ex:title \"Serd\"@en ;\n"
    "  ex:tags ( \"rdf\" \"turtle\" ) ;\n"
    "  ex:price \"9.5\"^^xsd:decimal ;\n"
    "  ex:open true .\n";

struct Memory {
  std::string text;
  std::size_t at;
};

std::size_t read_memory(void* buf, std::size_t size, std::size_t nmemb, void* stream) {
  Memory& memory = *static_cast<Memory*>(stream);
  const std::size_t n = std::min(size * nmemb, memory.text.size() - memory.at);
  std::memcpy(buf, memory.text.data() + memory.at, n);
  memory.at += n;
  return n;
}

int memory_error(void*) { return 0; }

std::size_t append(const void* buf, std::size_t len, void* stream) {
  static_cast<std::string*>(stream)->append(static_cast<const char*>(buf), len);
  return len;
}

struct Triples {
  SerdEnv* env;
  std::vector<std::string> lines;
};

std::string term(SerdEnv* env, const SerdNode* node) {
  const std::string str(reinterpret_cast<const char*>(node->buf), node->n_bytes);
  switch (node->type) {
    case SERD_CURIE: {
      SerdNode uri = serd_env_expand_node(env, node);
      const std::string expanded =
          uri.buf ? "<" + std::string(reinterpret_cast<const char*>(uri.buf)) + ">" : "?" + str;
      serd_node_free(&uri);
      return expanded;
    }
    case SERD_URI:
      return "<" + str + ">";
    case SERD_BLANK:
      return "_:" + str;
    default:
      return "\"" + str + "\"";
  }
}

SerdStatus on_prefix(void* handle, const SerdNode* name, const SerdNode* uri) {
  return serd_env_set_prefix(static_cast<Triples*>(handle)->env, name, uri);
}

SerdStatus on_statement(void* handle, SerdStatementFlags, const SerdNode*, const SerdNode* subject,
                        const SerdNode* predicate, const SerdNode* object,
                        const SerdNode* datatype, const SerdNode* lang) {
  Triples& triples = *static_cast<Triples*>(handle);
  std::string line = term(triples.env, subject) + " " + term(triples.env, predicate) + " " +
                     term(triples.env, object);
  if (lang && lang->buf) {
    line += "@" + std::string(reinterpret_cast<const char*>(lang->buf));
  }
  if (datatype && datatype->buf) {
    line += "^^" + term(triples.env, datatype);
  }
  triples.lines.push_back(line);
  return SERD_SUCCESS;
}

std::vector<std::string> read_triples(const std::string& text, std::size_t page_size) {
  Triples triples = {serd_env_new(nullptr), {}};
  SerdReader* reader =
      serd_reader_new(SERD_TURTLE, &triples, nullptr, nullptr, on_prefix, on_statement, nullptr);
  Memory memory = {text, 0};
  CHECK(serd_reader_read_source(reader, read_memory, memory_error, &memory,
                                reinterpret_cast<const std::uint8_t*>("memory"),
                                page_size) == SERD_SUCCESS);
  CHECK(memory.at == text.size());
  serd_reader_free(reader);
  serd_env_free(triples.env);
  return triples.lines;
}

SerdStatus write_prefix(void* handle, const SerdNode* name, const SerdNode* uri) {
  return serd_writer_set_prefix(static_cast<SerdWriter*>(handle), name, uri);
}

SerdStatus write_statement(void* handle, SerdStatementFlags flags, const SerdNode* graph,
                           const SerdNode* subject, const SerdNode* predicate,
                           const SerdNode* object, const SerdNode* datatype,
                           const SerdNode* lang) {
  return serd_writer_write_statement(static_cast<SerdWriter*>(handle), flags, graph, subject,
                                     predicate, object, datatype, lang);
}

SerdStatus write_end(void* handle, const SerdNode* node) {
  return serd_writer_end_anon(static_cast<SerdWriter*>(handle), node);
}

}  // namespace

int main() {
  const std::string rdf = "<http://www.w3.org/1999/02/22-rdf-syntax-ns#";
  const std::string xsd = "<http://www.w3.org/2001/XMLSchema#";
  const std::vector<std::string> expected = {
      "<http://example.org/ns#book> <http://example.org/ns#title> \"Serd\"@en",
      "<http://example.org/ns#book> <http://example.org/ns#tags> _:b1",
      "_:b1 " + rdf + "first> \"rdf\"",
      "_:b1 " + rdf + "rest> _:b2",
      "_:b2 " + rdf + "first> \"turtle\"",
      "_:b2 " + rdf + "rest> " + rdf + "nil>",
      "<http://example.org/ns#book> <http://example.org/ns#price> \"9.5\"^^" + xsd + "decimal>",
      "<http://example.org/ns#book> <http://example.org/ns#open> \"true\"^^" + xsd + "boolean>",
  };
  const std::string abbreviated =
      "@prefix ex: <http://example.org/ns#> .\n"
      "@prefix xsd: <http://www.w3.org/2001/XMLSchema#> .\n"
      "\n"
      "ex:book\n"
      "\tex:title \"Serd\"@en ;\n"
      "\tex:tags (\n"
      "\t\t\"rdf\"\n"
      "\t\t\"turtle\"\n"
      "\t) ;\n"
      "\tex:price 9.5 ;\n"
      "\tex:open true .\n";

  // Pages of 16 bytes split tokens across reads.
  const std::vector<std::string> parsed = read_triples(turtle, 16);
  CHECK(parsed == expected);
  if (parsed != expected) {
    for (const std::string& line : parsed) {
      std::fprintf(stderr, "%s\n", line.c_str());
    }
  }

  // Abbreviated Turtle keeps the CURIEs, the list and the bare literals; bulk
  // style buffers the output in pages until serd_writer_finish.
  std::string written;
  SerdEnv* env = serd_env_new(nullptr);
  SerdWriter* writer = serd_writer_new(
      SERD_TURTLE,
      static_cast<SerdStyle>(SERD_STYLE_ABBREVIATED | SERD_STYLE_CURIED | SERD_STYLE_BULK), env,
      nullptr, append, &written);
  SerdReader* reader = serd_reader_new(SERD_TURTLE, writer, nullptr, nullptr, write_prefix,
                                       write_statement, write_end);
  CHECK(serd_reader_read_string(reader, reinterpret_cast<const std::uint8_t*>(turtle)) ==
        SERD_SUCCESS);
  CHECK(written.empty());
  CHECK(serd_writer_finish(writer) == SERD_SUCCESS);
  serd_reader_free(reader);
  serd_writer_free(writer);
  serd_env_free(env);
  CHECK(written == abbreviated);
  if (written != abbreviated) {
    std::fprintf(stderr, "Turtle output:\n%s", written.c_str());
  }
  CHECK(read_triples(written, 4096) == expected);

  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("serd C++ consumer test passed");
  return 0;
}
