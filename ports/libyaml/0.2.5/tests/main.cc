// Consumer test from C++: load a multi-document stream through yaml.h's
// extern "C" API into std::map, and emit events into a buffer.
#include <yaml.h>

#include <cstdio>
#include <cstring>
#include <map>
#include <string>

namespace {

int failures = 0;

void check(bool ok, const char* what, int line) {
  if (!ok) {
    std::fprintf(stderr, "main.cc:%d: check failed: %s\n", line, what);
    ++failures;
  }
}
#define CHECK(cond) check((cond), #cond, __LINE__)

std::string text(yaml_document_t* doc, int index) {
  yaml_node_t* node = yaml_document_get_node(doc, index);
  if (node == 0 || node->type != YAML_SCALAR_NODE) return "";
  return std::string(reinterpret_cast<const char*>(node->data.scalar.value),
                     node->data.scalar.length);
}

}  // namespace

int main() {
  const char input[] =
      "---\nhost: example.org\nport: 8080\n"
      "---\nquote: \"tab\\there\"\nblock: |\n  line one\n  line two\n";
  yaml_parser_t parser;
  CHECK(yaml_parser_initialize(&parser) != 0);
  yaml_parser_set_input_string(
      &parser, reinterpret_cast<const unsigned char*>(input),
      sizeof input - 1);

  std::map<std::string, std::string> values;
  int documents = 0;
  for (;;) {
    yaml_document_t doc;
    if (!yaml_parser_load(&parser, &doc)) {
      CHECK(!"yaml_parser_load failed");
      break;
    }
    yaml_node_t* root = yaml_document_get_root_node(&doc);
    if (root == 0) {  // an empty document marks the end of the stream
      yaml_document_delete(&doc);
      break;
    }
    ++documents;
    if (root->type == YAML_MAPPING_NODE) {
      for (yaml_node_pair_t* pair = root->data.mapping.pairs.start;
           pair < root->data.mapping.pairs.top; ++pair)
        values[text(&doc, pair->key)] = text(&doc, pair->value);
    }
    yaml_document_delete(&doc);
  }
  yaml_parser_delete(&parser);

  CHECK(documents == 2);
  CHECK(values["host"] == "example.org");
  CHECK(values["port"] == "8080");
  CHECK(values["quote"] == "tab\there");
  CHECK(values["block"] == "line one\nline two\n");

  unsigned char out[128];
  size_t written = 0;
  yaml_emitter_t emitter;
  yaml_event_t event;
  CHECK(yaml_emitter_initialize(&emitter) != 0);
  yaml_emitter_set_output_string(&emitter, out, sizeof out - 1, &written);
  yaml_stream_start_event_initialize(&event, YAML_UTF8_ENCODING);
  CHECK(yaml_emitter_emit(&emitter, &event) != 0);
  yaml_document_start_event_initialize(&event, 0, 0, 0, 1);
  CHECK(yaml_emitter_emit(&emitter, &event) != 0);
  yaml_scalar_event_initialize(
      &event, 0, 0, reinterpret_cast<yaml_char_t*>(const_cast<char*>("a:b")),
      -1, 1, 1, YAML_SINGLE_QUOTED_SCALAR_STYLE);
  CHECK(yaml_emitter_emit(&emitter, &event) != 0);
  yaml_document_end_event_initialize(&event, 1);
  CHECK(yaml_emitter_emit(&emitter, &event) != 0);
  yaml_stream_end_event_initialize(&event);
  CHECK(yaml_emitter_emit(&emitter, &event) != 0);
  yaml_emitter_delete(&emitter);
  out[written] = '\0';
  CHECK(std::strcmp(reinterpret_cast<const char*>(out), "'a:b'\n") == 0);

  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("libyaml C++ consumer test passed");
  return 0;
}
