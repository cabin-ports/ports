/* Names only the extensions library: the core reaches it through its public
   edge. */
#include <cmark-gfm-core-extensions.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
  const char *md = "~gone~ and https://example.org/x.\n";
  const char *want =
      "<p><del>gone</del> and <a href=\"https://example.org/x\">https://example.org/x</a>.</p>\n";
  cmark_syntax_extension *strike;
  cmark_syntax_extension *autolink;
  cmark_parser *parser;
  cmark_node *doc;
  char *html;
  int ok;

  cmark_gfm_core_extensions_ensure_registered();
  strike = cmark_find_syntax_extension("strikethrough");
  autolink = cmark_find_syntax_extension("autolink");
  if (strike == NULL || autolink == NULL) {
    fprintf(stderr, "FAIL: extensions not registered\n");
    return EXIT_FAILURE;
  }
  parser = cmark_parser_new(CMARK_OPT_DEFAULT);
  cmark_parser_attach_syntax_extension(parser, strike);
  cmark_parser_attach_syntax_extension(parser, autolink);
  cmark_parser_feed(parser, md, strlen(md));
  doc = cmark_parser_finish(parser);
  html = cmark_render_html(doc, CMARK_OPT_DEFAULT, cmark_parser_get_syntax_extensions(parser));
  ok = html != NULL && strcmp(html, want) == 0;
  if (!ok) {
    fprintf(stderr, "FAIL: html: %s\n", html ? html : "(null)");
  }
  cmark_get_default_mem_allocator()->free(html);
  cmark_node_free(doc);
  cmark_parser_free(parser);
  if (!ok) {
    return EXIT_FAILURE;
  }
  puts("cmark-gfm extensions probe: ok");
  return EXIT_SUCCESS;
}
