#include <cmark-gfm-core-extensions.h>
#include <cmark-gfm.h>

#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <string>

namespace {

int failures = 0;

void check(bool ok, const std::string &what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what.c_str());
    ++failures;
  }
}

void check_equal(const std::string &got, const std::string &want, const char *what) {
  check(got == want, std::string(what) + "\n got: " + got + "\nwant: " + want);
}

std::string take(char *text) {
  std::string copy = text != nullptr ? text : "(null)";
  cmark_get_default_mem_allocator()->free(text);
  return copy;
}

}  // namespace

int main() {
  check(cmark_version() == CMARK_GFM_VERSION, "cmark_version() == CMARK_GFM_VERSION");
  check(cmark_version() == 1900558, "cmark_version()");
  check_equal(cmark_version_string(), "0.29.0.gfm.14", "cmark_version_string()");

  const std::string entity = "&copy; &#x41;&amp;\n";
  check_equal(take(cmark_markdown_to_html(entity.data(), entity.size(), CMARK_OPT_DEFAULT)),
              "<p>\xC2\xA9 A&amp;</p>\n", "entities");

  cmark_gfm_core_extensions_ensure_registered();
  cmark_parser *parser = cmark_parser_new(CMARK_OPT_DEFAULT);
  for (const char *name : {"table", "strikethrough"}) {
    cmark_syntax_extension *ext = cmark_find_syntax_extension(name);
    check(ext != nullptr && cmark_parser_attach_syntax_extension(parser, ext) == 1, name);
  }
  const std::string md = "## Stock\n\n| item | note |\n| --- | --- |\n| ~old~ | `new` |\n";
  cmark_parser_feed(parser, md.data(), md.size());
  cmark_node *doc = cmark_parser_finish(parser);

  std::string walk;
  cmark_iter *iter = cmark_iter_new(doc);
  cmark_event_type event;
  while ((event = cmark_iter_next(iter)) != CMARK_EVENT_DONE) {
    if (event == CMARK_EVENT_ENTER) {
      cmark_node *node = cmark_iter_get_node(iter);
      walk += cmark_node_get_type_string(node);
      const char *literal = cmark_node_get_literal(node);
      if (literal != nullptr) {
        walk += std::string("(") + literal + ")";
      }
      walk += ' ';
    }
  }
  cmark_iter_free(iter);
  check_equal(walk,
              "document heading text(Stock) table table_header table_cell text(item) "
              "table_cell text(note) table_row table_cell strikethrough text(old) "
              "table_cell code(new) ",
              "tree walk");

  cmark_node *heading = cmark_node_first_child(doc);
  check(heading != nullptr && cmark_node_get_heading_level(heading) == 2, "heading level");
  cmark_node *table = heading != nullptr ? cmark_node_next(heading) : nullptr;
  check(table != nullptr && cmark_gfm_extensions_get_table_columns(table) == 2,
        "table columns");

  check_equal(take(cmark_render_html(doc, CMARK_OPT_DEFAULT,
                                     cmark_parser_get_syntax_extensions(parser))),
              "<h2>Stock</h2>\n<table>\n<thead>\n<tr>\n<th>item</th>\n<th>note</th>\n"
              "</tr>\n</thead>\n<tbody>\n<tr>\n<td><del>old</del></td>\n"
              "<td><code>new</code></td>\n</tr>\n</tbody>\n</table>\n",
              "html");

  cmark_node_free(doc);
  cmark_parser_free(parser);

  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return EXIT_FAILURE;
  }
  std::puts("cmark-gfm C++ consumer: ok");
  return EXIT_SUCCESS;
}
