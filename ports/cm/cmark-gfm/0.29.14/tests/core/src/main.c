/* Links the core library alone. */
#include <cmark-gfm.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
  const char *md = "# Notes\n\n1. one\n2. two\n";
  char *html = cmark_markdown_to_html(md, strlen(md), CMARK_OPT_DEFAULT);
  int ok = html != NULL &&
           strcmp(html, "<h1>Notes</h1>\n<ol>\n<li>one</li>\n<li>two</li>\n</ol>\n") == 0;

  if (!ok) {
    fprintf(stderr, "FAIL: html: %s\n", html ? html : "(null)");
  }
  cmark_get_default_mem_allocator()->free(html);
  if (cmark_version() != CMARK_GFM_VERSION) {
    fprintf(stderr, "FAIL: cmark_version()\n");
    ok = 0;
  }
  if (!ok) {
    return EXIT_FAILURE;
  }
  puts("cmark-gfm core probe: ok");
  return EXIT_SUCCESS;
}
