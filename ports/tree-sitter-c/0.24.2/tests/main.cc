// The C++ consumer: upstream's corpus case test/corpus/expressions.txt
// "Function calls", whose recorded tree names no fields, compared against
// an s-expression of the named nodes built with a tree cursor; then the
// callees and string literals through a query.
#include <tree_sitter/api.h>
#include <tree_sitter/tree-sitter-c.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#define CHECK(cond)                                                    \
  do {                                                                 \
    if (!(cond)) {                                                     \
      std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__,      \
                   __LINE__, #cond);                                   \
      std::exit(1);                                                    \
    }                                                                  \
  } while (0)

static const char source[] =
    "int main() {\n"
    "  printf(\"hi! %d\\n\", x);\n"
    "  __assert_fail(\"some_error_message\", 115, __extension__ __func__);\n"
    "}\n";

static const char expected[] =
    "(translation_unit (function_definition (primitive_type) "
    "(function_declarator (identifier) (parameter_list)) (compound_statement "
    "(expression_statement (call_expression (identifier) (argument_list "
    "(string_literal (string_content) (escape_sequence)) (identifier)))) "
    "(expression_statement (call_expression (identifier) (argument_list "
    "(string_literal (string_content)) (number_literal) (extension_expression "
    "(identifier))))))))";

// The named nodes under the cursor's node, without field names.
static void sexp(TSTreeCursor* cursor, std::string& out) {
  TSNode node = ts_tree_cursor_current_node(cursor);
  bool named = ts_node_is_named(node);
  if (named) {
    if (!out.empty()) out += ' ';
    out += '(';
    out += ts_node_type(node);
  }
  if (ts_tree_cursor_goto_first_child(cursor)) {
    do sexp(cursor, out);
    while (ts_tree_cursor_goto_next_sibling(cursor));
    ts_tree_cursor_goto_parent(cursor);
  }
  if (named) out += ')';
}

static std::string text(TSNode node) {
  return std::string(source + ts_node_start_byte(node),
                     ts_node_end_byte(node) - ts_node_start_byte(node));
}

int main() {
  TSParser* parser = ts_parser_new();
  CHECK(ts_parser_set_language(parser, tree_sitter_c()));
  TSTree* tree = ts_parser_parse_string(
      parser, nullptr, source, static_cast<std::uint32_t>(std::strlen(source)));
  CHECK(tree != nullptr);
  TSNode root = ts_tree_root_node(tree);
  CHECK(!ts_node_has_error(root));

  TSTreeCursor cursor = ts_tree_cursor_new(root);
  std::string tree_text;
  sexp(&cursor, tree_text);
  ts_tree_cursor_delete(&cursor);
  if (tree_text != expected) std::fprintf(stderr, "got: %s\n", tree_text.c_str());
  CHECK(tree_text == expected);

  const char pattern[] =
      "(call_expression function: (identifier) @callee)"
      "(string_literal) @string";
  std::uint32_t error_offset;
  TSQueryError error;
  TSQuery* query = ts_query_new(tree_sitter_c(), pattern,
                                static_cast<std::uint32_t>(std::strlen(pattern)),
                                &error_offset, &error);
  CHECK(query != nullptr && error == TSQueryErrorNone);
  CHECK(ts_query_pattern_count(query) == 2 &&
        ts_query_capture_count(query) == 2);
  TSQueryCursor* query_cursor = ts_query_cursor_new();
  ts_query_cursor_exec(query_cursor, query, root);
  std::string callees, strings;
  TSQueryMatch match;
  while (ts_query_cursor_next_match(query_cursor, &match)) {
    CHECK(match.capture_count == 1);
    std::string& list = match.pattern_index == 0 ? callees : strings;
    if (!list.empty()) list += ' ';
    list += text(match.captures[0].node);
  }
  CHECK(callees == "printf __assert_fail");
  CHECK(strings == "\"hi! %d\\n\" \"some_error_message\"");
  ts_query_cursor_delete(query_cursor);
  ts_query_delete(query);

  ts_tree_delete(tree);
  ts_parser_delete(parser);
  std::puts("ok");
  return 0;
}
