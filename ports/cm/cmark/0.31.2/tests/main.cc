/* The C++ consumer: the version macros and functions, Markdown to HTML with
 * case-folded link labels and named entities, and the parser and tree API
 * with a CommonMark render. */
#include <cmark.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

static bool ok = true;

static void check(bool pass, const char *what) {
    if (!pass) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ok = false;
    }
}

static void html(const char *markdown, const char *expected, const char *what) {
    char *out = cmark_markdown_to_html(markdown, std::strlen(markdown), CMARK_OPT_DEFAULT);
    check(out && std::strcmp(out, expected) == 0, what);
    std::free(out);
}

int main() {
    check(CMARK_VERSION == 7938 && cmark_version() == 7938, "version number");
    check(std::strcmp(CMARK_VERSION_STRING, "0.31.2") == 0 && std::strcmp(cmark_version_string(), "0.31.2") == 0,
          "version string");

    html("# Hi\n\n*a* **b**\n", "<h1>Hi</h1>\n<p><em>a</em> <strong>b</strong></p>\n", "headings and emphasis");
    /* U+1E9E folds to "ss", so [SS] defines the reference (src/case_fold.inc). */
    html("[\xE1\xBA\x9E]\n\n[SS]: /url\n", "<p><a href=\"/url\">\xE1\xBA\x9E</a></p>\n", "case-folded reference");
    /* src/entities.inc */
    html("&ouml; &copy;\n", "<p>\xC3\xB6 \xC2\xA9</p>\n", "named entities");

    cmark_parser *parser = cmark_parser_new(CMARK_OPT_DEFAULT);
    cmark_parser_feed(parser, "# H", 3);
    cmark_parser_feed(parser, "i\n", 2);
    cmark_node *doc = cmark_parser_finish(parser);
    cmark_node *first = cmark_node_first_child(doc);
    check(cmark_node_get_type(doc) == CMARK_NODE_DOCUMENT, "document node");
    check(first && cmark_node_get_type(first) == CMARK_NODE_HEADING, "heading node");
    check(first && cmark_node_get_heading_level(first) == 1, "heading level");
    char *md = cmark_render_commonmark(doc, CMARK_OPT_DEFAULT, 0);
    check(md && std::strcmp(md, "# Hi\n") == 0, "commonmark render");
    std::free(md);
    cmark_node_free(doc);
    cmark_parser_free(parser);

    if (ok) std::puts("ok");
    return ok ? 0 : 1;
}
