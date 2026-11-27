#include <cmark-gfm-core-extensions.h>
#include <cmark-gfm.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

static void check(int ok, const char *what) {
  if (!ok) {
    fprintf(stderr, "FAIL: %s\n", what);
    failures++;
  }
}

static void check_html(const char *got, const char *want, const char *what) {
  if (got == NULL || strcmp(got, want) != 0) {
    fprintf(stderr, "FAIL: %s\n got: %s\nwant: %s\n", what, got ? got : "(null)",
            want);
    failures++;
  }
}

/* Parses md with every core extension attached and renders it as HTML. */
static char *render(const char *md, int options, cmark_node **doc_out) {
  static const char *names[] = {"table", "strikethrough", "autolink", "tagfilter",
                                "tasklist"};
  cmark_parser *parser = cmark_parser_new(options);
  cmark_node *doc;
  char *html;
  size_t i;

  for (i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
    cmark_syntax_extension *ext = cmark_find_syntax_extension(names[i]);
    check(ext != NULL, names[i]);
    if (ext != NULL) {
      check(cmark_parser_attach_syntax_extension(parser, ext) == 1, names[i]);
    }
  }
  cmark_parser_feed(parser, md, strlen(md));
  doc = cmark_parser_finish(parser);
  html = cmark_render_html(doc, options, cmark_parser_get_syntax_extensions(parser));
  cmark_parser_free(parser);
  if (doc_out != NULL) {
    *doc_out = doc;
  } else {
    cmark_node_free(doc);
  }
  return html;
}

int main(void) {
  cmark_mem *mem = cmark_get_default_mem_allocator();
  cmark_node *doc;
  cmark_node *node;
  char *html;
  const char *md;

  check(cmark_version() == CMARK_GFM_VERSION, "cmark_version() == CMARK_GFM_VERSION");
  check(cmark_version() == ((0 << 24) | (29 << 16) | (0 << 8) | 14), "cmark_version()");
  check(strcmp(cmark_version_string(), "0.29.0.gfm.14") == 0, "cmark_version_string()");
  check(strcmp(CMARK_GFM_VERSION_STRING, "0.29.0.gfm.14") == 0,
        "CMARK_GFM_VERSION_STRING");

  md = "Hello *world*\n";
  html = cmark_markdown_to_html(md, strlen(md), CMARK_OPT_DEFAULT);
  check_html(html, "<p>Hello <em>world</em></p>\n", "emphasis");
  mem->free(html);

  /* U+1E9E LATIN CAPITAL LETTER SHARP S case-folds to "ss". */
  md = "[\xE1\xBA\x9E]\n\n[SS]: /url\n";
  html = cmark_markdown_to_html(md, strlen(md), CMARK_OPT_DEFAULT);
  check_html(html, "<p><a href=\"/url\">\xE1\xBA\x9E</a></p>\n", "case-folded link label");
  mem->free(html);

  /* Without the extensions registered, tildes stay text. */
  check(cmark_find_syntax_extension("strikethrough") == NULL, "unregistered extension");
  md = "Was ~cheap~ free.\n";
  html = cmark_markdown_to_html(md, strlen(md), CMARK_OPT_DEFAULT);
  check_html(html, "<p>Was ~cheap~ free.</p>\n", "plain tildes");
  mem->free(html);

  cmark_gfm_core_extensions_ensure_registered();

  html = render("| fruit | qty |\n| :--- | ---: |\n| apple | 3 |\n| pear | 12 |\n",
                CMARK_OPT_DEFAULT, &doc);
  check_html(html,
             "<table>\n<thead>\n<tr>\n<th align=\"left\">fruit</th>\n"
             "<th align=\"right\">qty</th>\n</tr>\n</thead>\n<tbody>\n<tr>\n"
             "<td align=\"left\">apple</td>\n<td align=\"right\">3</td>\n</tr>\n"
             "<tr>\n<td align=\"left\">pear</td>\n<td align=\"right\">12</td>\n"
             "</tr>\n</tbody>\n</table>\n",
             "table");
  mem->free(html);
  node = cmark_node_first_child(doc);
  check(node != NULL && strcmp(cmark_node_get_type_string(node), "table") == 0,
        "table node");
  if (node != NULL) {
    uint8_t *align = cmark_gfm_extensions_get_table_alignments(node);
    check(cmark_gfm_extensions_get_table_columns(node) == 2, "table columns");
    check(align != NULL && align[0] == 'l' && align[1] == 'r', "table alignments");
  }
  cmark_node_free(doc);

  html = render("Was ~cheap~ free.\n", CMARK_OPT_DEFAULT, NULL);
  check_html(html, "<p>Was <del>cheap</del> free.</p>\n", "strikethrough");
  mem->free(html);

  html = render("See www.example.com today.\n", CMARK_OPT_DEFAULT, NULL);
  check_html(html,
             "<p>See <a href=\"http://www.example.com\">www.example.com</a> today.</p>\n",
             "autolink");
  mem->free(html);

  html = render("Keep <em>this</em>, not <xmp>.\n", CMARK_OPT_UNSAFE, NULL);
  check_html(html, "<p>Keep <em>this</em>, not &lt;xmp>.</p>\n", "tagfilter");
  mem->free(html);

  html = render("- [x] done\n- [ ] todo\n", CMARK_OPT_DEFAULT, &doc);
  check_html(html,
             "<ul>\n<li><input type=\"checkbox\" checked=\"\" disabled=\"\" /> done</li>\n"
             "<li><input type=\"checkbox\" disabled=\"\" /> todo</li>\n</ul>\n",
             "tasklist");
  mem->free(html);
  node = cmark_node_first_child(doc);
  node = node != NULL ? cmark_node_first_child(node) : NULL;
  check(node != NULL && cmark_gfm_extensions_get_tasklist_item_checked(node),
        "first item checked");
  node = node != NULL ? cmark_node_next(node) : NULL;
  check(node != NULL && !cmark_gfm_extensions_get_tasklist_item_checked(node),
        "second item unchecked");
  cmark_node_free(doc);

  if (failures != 0) {
    fprintf(stderr, "%d check(s) failed\n", failures);
    return EXIT_FAILURE;
  }
  puts("cmark-gfm C consumer: ok");
  return EXIT_SUCCESS;
}
