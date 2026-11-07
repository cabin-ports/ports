/* The C consumer: the C grammar driven by the tree-sitter runtime.  Two
 * cases from upstream's test corpus (test/corpus/declarations.txt
 * "Function definitions" and test/corpus/preprocessor.txt "Include
 * directives") must parse to the trees the corpus records, here with
 * the whitespace collapsed; the test then navigates fields, runs
 * queries, reparses after an edit, and recovers from a syntax error. */
#include <tree_sitter/api.h>
#include <tree_sitter/tree-sitter-c.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond)                                                    \
  do {                                                                 \
    if (!(cond)) {                                                     \
      fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, \
              #cond);                                                  \
      exit(1);                                                         \
    }                                                                  \
  } while (0)

static const char functions[] =
    "void * do_stuff(int arg1) {\n"
    "  return 5;\n"
    "}\n"
    "\n"
    "// K&R style\n"
    "int foo(bar, baz, qux)\n"
    "int bar, baz;\n"
    "char *qux;\n"
    "{\n"
    "}\n";

static const char functions_tree[] =
    "(translation_unit (function_definition type: (primitive_type) "
    "declarator: (pointer_declarator declarator: (function_declarator "
    "declarator: (identifier) parameters: (parameter_list "
    "(parameter_declaration type: (primitive_type) declarator: "
    "(identifier))))) body: (compound_statement (return_statement "
    "(number_literal)))) (comment) (function_definition type: "
    "(primitive_type) declarator: (function_declarator declarator: "
    "(identifier) parameters: (parameter_list (identifier) (identifier) "
    "(identifier))) (declaration type: (primitive_type) declarator: "
    "(identifier) declarator: (identifier)) (declaration type: "
    "(primitive_type) declarator: (pointer_declarator declarator: "
    "(identifier))) body: (compound_statement)))";

static const char includes[] =
    "#include \"some/path.h\"\n"
    "#include <stdint.h>\n"
    "#include MACRO\n"
    "#include MACRO(arg1, arg2)\n";

static const char includes_tree[] =
    "(translation_unit (preproc_include path: (string_literal "
    "(string_content))) (preproc_include path: (system_lib_string)) "
    "(preproc_include path: (identifier)) (preproc_include path: "
    "(call_expression function: (identifier) arguments: (argument_list "
    "(identifier) (identifier)))))";

static TSTree *parse(TSParser *parser, const char *source) {
  TSTree *tree =
      ts_parser_parse_string(parser, NULL, source, (uint32_t)strlen(source));
  CHECK(tree != NULL);
  return tree;
}

static bool tree_is(TSTree *tree, const char *expected) {
  char *string = ts_node_string(ts_tree_root_node(tree));
  bool same = strcmp(string, expected) == 0;
  if (!same) fprintf(stderr, "got: %s\n", string);
  free(string);
  return same;
}

static bool text_is(TSNode node, const char *source, const char *expected) {
  uint32_t start = ts_node_start_byte(node), end = ts_node_end_byte(node);
  return !ts_node_is_null(node) && end - start == strlen(expected) &&
         memcmp(source + start, expected, end - start) == 0;
}

static TSNode field(TSNode node, const char *name) {
  return ts_node_child_by_field_name(node, name, (uint32_t)strlen(name));
}

/* Writes the text of every capture of `pattern` in `source`, separated
 * by spaces, to `out`. */
static void captures(const TSLanguage *language, TSTree *tree,
                     const char *source, const char *pattern, char *out,
                     size_t size) {
  uint32_t error_offset;
  TSQueryError error;
  TSQuery *query = ts_query_new(language, pattern, (uint32_t)strlen(pattern),
                                &error_offset, &error);
  CHECK(query != NULL && error == TSQueryErrorNone);
  TSQueryCursor *cursor = ts_query_cursor_new();
  ts_query_cursor_exec(cursor, query, ts_tree_root_node(tree));
  TSQueryMatch match;
  uint32_t index;
  out[0] = '\0';
  while (ts_query_cursor_next_capture(cursor, &match, &index)) {
    TSNode node = match.captures[index].node;
    uint32_t start = ts_node_start_byte(node), end = ts_node_end_byte(node);
    size_t used = strlen(out);
    CHECK(used + 1 + (end - start) < size);
    if (used > 0) out[used++] = ' ';
    memcpy(out + used, source + start, end - start);
    out[used + (end - start)] = '\0';
  }
  ts_query_cursor_delete(cursor);
  ts_query_delete(query);
}

int main(void) {
  const TSLanguage *c = tree_sitter_c();
  CHECK(ts_language_abi_version(c) == 15);
  CHECK(strcmp(ts_language_name(c), "c") == 0);
  const TSLanguageMetadata *metadata = ts_language_metadata(c);
  CHECK(metadata != NULL && metadata->major_version == 0 &&
        metadata->minor_version == 24 && metadata->patch_version == 2);
  CHECK(ts_language_symbol_for_name(c, "function_definition", 19, true) != 0);
  CHECK(ts_language_field_id_for_name(c, "declarator", 10) != 0);

  TSParser *parser = ts_parser_new();
  CHECK(ts_parser_set_language(parser, c));

  /* The corpus cases. */
  TSTree *tree = parse(parser, functions);
  CHECK(tree_is(tree, functions_tree));
  CHECK(!ts_node_has_error(ts_tree_root_node(tree)));
  TSNode first = ts_node_named_child(ts_tree_root_node(tree), 0);
  TSNode function = field(field(first, "declarator"), "declarator");
  CHECK(strcmp(ts_node_type(function), "function_declarator") == 0);
  CHECK(text_is(field(function, "declarator"), functions, "do_stuff"));
  CHECK(text_is(field(function, "parameters"), functions, "(int arg1)"));
  CHECK(text_is(field(first, "body"), functions, "{\n  return 5;\n}"));

  char out[128];
  captures(c, tree, functions,
           "(function_declarator declarator: (identifier) @name)", out,
           sizeof out);
  CHECK(strcmp(out, "do_stuff foo") == 0);
  captures(c, tree, functions,
           "(declaration declarator: [(identifier) @old"
           " (pointer_declarator declarator: (identifier) @old)])",
           out, sizeof out);
  CHECK(strcmp(out, "bar baz qux") == 0);
  ts_tree_delete(tree);

  tree = parse(parser, includes);
  CHECK(tree_is(tree, includes_tree));
  captures(c, tree, includes, "(preproc_include path: (_) @path)", out,
           sizeof out);
  CHECK(strcmp(out, "\"some/path.h\" <stdint.h> MACRO MACRO(arg1, arg2)") ==
        0);
  ts_tree_delete(tree);

  /* An edit: `1 + 2` becomes `1 * 2`, reusing the old tree. */
  char source[] = "int x = 1 + 2;\n";
  tree = parse(parser, source);
  CHECK(tree_is(tree,
                "(translation_unit (declaration type: (primitive_type) "
                "declarator: (init_declarator declarator: (identifier) "
                "value: (binary_expression left: (number_literal) "
                "right: (number_literal)))))"));
  source[10] = '*';
  TSInputEdit edit;
  edit.start_byte = 10;
  edit.old_end_byte = 11;
  edit.new_end_byte = 11;
  edit.start_point.row = 0;
  edit.start_point.column = 10;
  edit.old_end_point.row = 0;
  edit.old_end_point.column = 11;
  edit.new_end_point.row = 0;
  edit.new_end_point.column = 11;
  ts_tree_edit(tree, &edit);
  TSTree *edited = ts_parser_parse_string(parser, tree, source,
                                          (uint32_t)strlen(source));
  CHECK(edited != NULL);
  TSNode value = field(
      field(ts_node_named_child(ts_tree_root_node(edited), 0), "declarator"),
      "value");
  CHECK(strcmp(ts_node_type(field(value, "operator")), "*") == 0);
  CHECK(text_is(value, source, "1 * 2"));
  ts_tree_delete(edited);
  ts_tree_delete(tree);

  /* A syntax error: the parser recovers and marks the tree. */
  const char *broken = "int f( {\n  return 1;\n}\n";
  tree = parse(parser, broken);
  CHECK(ts_node_has_error(ts_tree_root_node(tree)));
  ts_tree_delete(tree);

  ts_parser_delete(parser);
  puts("ok");
  return 0;
}
