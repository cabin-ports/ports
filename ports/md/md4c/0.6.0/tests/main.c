/* The C consumer: md4c-html renders named and numeric entities, link
 * labels matched under Unicode case folding, CommonMark blocks and spans,
 * and the extensions (tables, task lists, strikethrough, highlight,
 * preserved blank lines, XHTML), and md4c's parser reports document
 * structure and details through its callbacks and stops when a callback
 * asks it to. */
#include <md4c-html.h>
#include <md4c.h>

#include <stdio.h>
#include <string.h>

static int ok = 1;

static void check(int pass, const char *what) {
    if (!pass) {
        fprintf(stderr, "FAIL: %s\n", what);
        ok = 0;
    }
}

typedef struct {
    char data[1024];
    size_t size;
    int overflow;
} Buffer;

static void append(const MD_CHAR *text, MD_SIZE size, void *userdata) {
    Buffer *b = userdata;
    if (b->size + size >= sizeof b->data) {
        b->overflow = 1;
        return;
    }
    memcpy(b->data + b->size, text, size);
    b->size += size;
    b->data[b->size] = '\0';
}

static void html(const char *markdown, unsigned parser_flags, unsigned render_flags, const char *expected,
                 const char *what) {
    Buffer b;
    int rc;
    memset(&b, 0, sizeof b);
    rc = md_html(markdown, (MD_SIZE)strlen(markdown), append, &b, parser_flags, render_flags);
    check(rc == 0 && !b.overflow && strcmp(b.data, expected) == 0, what);
    if (rc == 0 && strcmp(b.data, expected) != 0) fprintf(stderr, "  got: %s\n", b.data);
}

static void rendering(void) {
    /* &ngE; is one of the entities that expand to two code points, and
     * &rarr; sits late in the sorted entity table. */
    html("&copy; 2026 &mdash; caf&eacute; &frac12; &amp; &#8364; &#x1F600; &ngE; &rarr;\n", 0, 0,
         "<p>\xc2\xa9 2026 \xe2\x80\x94 caf\xc3\xa9 \xc2\xbd &amp; \xe2\x82\xac \xf0\x9f\x98\x80 "
         "\xe2\x89\xa7\xcc\xb8 \xe2\x86\x92</p>\n",
         "entities decode to UTF-8");
    html("&copy; &#35;\n", 0, MD_HTML_FLAG_VERBATIM_ENTITIES, "<p>&copy; &#35;</p>\n",
         "entities stay verbatim on request");
    /* German "Strasse" with a sharp s, which folds to "ss", and Greek
     * "Sisyphos" in capitals, defined in small letters ending in final
     * sigma, which folds like the other two sigmas. */
    html("[Stra\xc3\x9f" "e] and [\xce\xa3\xce\x8a\xce\xa3\xce\xa5\xce\xa6\xce\x9f\xce\xa3]\n\n"
         "[STRASSE]: /street\n[\xcf\x83\xce\xaf\xcf\x83\xcf\x85\xcf\x86\xce\xbf\xcf\x82]: /myth\n",
         0, 0,
         "<p><a href=\"/street\">Stra\xc3\x9f" "e</a> and <a href=\"/myth\">"
         "\xce\xa3\xce\x8a\xce\xa3\xce\xa5\xce\xa6\xce\x9f\xce\xa3</a></p>\n",
         "link labels match under Unicode case folding");
    html("# Title\n\nSome *emphasis*, **strong** and `code`.\n\n> quoted\n", 0, 0,
         "<h1>Title</h1>\n<p>Some <em>emphasis</em>, <strong>strong</strong> and <code>code</code>.</p>\n"
         "<blockquote>\n<p>quoted</p>\n</blockquote>\n",
         "CommonMark blocks and spans");
    /* Image descriptions land in alt="...", so raw HTML there is escaped. */
    html("3. three\n4. *four*\n\n```python\nx < 1\n```\n\n![a <b>x</b> 'y'](/i.png \"T\")\n", 0, 0,
         "<ol start=\"3\">\n<li>three</li>\n<li><em>four</em></li>\n</ol>\n"
         "<pre><code class=\"language-python\">x &lt; 1\n</code></pre>\n"
         "<p><img src=\"/i.png\" alt=\"a &lt;b&gt;x&lt;/b&gt; &#x27;y&#x27;\" title=\"T\"></p>\n",
         "an ordered list, a fenced code block and an image");
    html("| Name | Lang | Year | Stars |\n|------|:-----|:----:|------:|\n| md4c | C | 2016 | 900 |\n",
         MD_FLAG_TABLES, 0,
         "<table>\n<thead>\n<tr>\n<th>Name</th>\n<th align=\"left\">Lang</th>\n"
         "<th align=\"center\">Year</th>\n<th align=\"right\">Stars</th>\n</tr>\n</thead>\n"
         "<tbody>\n<tr>\n<td>md4c</td>\n<td align=\"left\">C</td>\n<td align=\"center\">2016</td>\n"
         "<td align=\"right\">900</td>\n</tr>\n</tbody>\n</table>\n",
         "a table with column alignment");
    html("| a |\n|---|\n", 0, 0, "<p>| a |\n|---|</p>\n", "tables need MD_FLAG_TABLES");
    html("- [x] write the port\n- [ ] publish it\n", MD_FLAG_TASKLISTS, 0,
         "<ul>\n<li class=\"task-list-item\"><input type=\"checkbox\" class=\"task-list-item-checkbox\" disabled "
         "checked>write the port</li>\n<li class=\"task-list-item\"><input type=\"checkbox\" "
         "class=\"task-list-item-checkbox\" disabled>publish it</li>\n</ul>\n",
         "a task list");
    html("Ship it ~today~ tomorrow.\n", MD_FLAG_STRIKETHROUGH, 0, "<p>Ship it <del>today</del> tomorrow.</p>\n",
         "strikethrough");
    html("Read the ==license== first.\n", MD_FLAG_HIGHLIGHT, 0, "<p>Read the <mark>license</mark> first.</p>\n",
         "highlight");
    html("==note==\n", 0, 0, "<p>==note==</p>\n", "highlight needs MD_FLAG_HIGHLIGHT");
    html("Top.\n\n\n\nBottom.\n", MD_FLAG_PRESERVEBLANKLINES, 0, "<p>Top.</p>\n<br>\n<br>\n<p>Bottom.</p>\n",
         "preserved blank lines");
    html("Top.\n\n\n\nBottom.\n", 0, 0, "<p>Top.</p>\n<p>Bottom.</p>\n",
         "blank lines collapse without MD_FLAG_PRESERVEBLANKLINES");
    html("foo  \nbar\n\n***\n", 0, MD_HTML_FLAG_XHTML, "<p>foo<br />\nbar</p>\n<hr />\n", "XHTML output");
}

/* A parser client that records what the callbacks report. */
typedef struct {
    int blocks[MD_BLOCK_BLANK + 1];
    int spans[MD_SPAN_MARK + 1];
    int depth, max_depth, unbalanced;
    unsigned heading_level, blank_lines, table_columns;
    MD_ALIGN last_align;
    char href[64], lang[16], text[256];
    int abort_at_heading;
} Record;

static void copy_attribute(char *out, size_t capacity, const MD_ATTRIBUTE *a) {
    size_t n = a->size < capacity - 1 ? a->size : capacity - 1;
    memcpy(out, a->text, n);
    out[n] = '\0';
}

static int enter_block(MD_BLOCKTYPE type, void *detail, void *userdata) {
    Record *r = userdata;
    r->blocks[type]++;
    if (++r->depth > r->max_depth) r->max_depth = r->depth;
    switch (type) {
    case MD_BLOCK_H:
        r->heading_level = ((MD_BLOCK_H_DETAIL *)detail)->level;
        if (r->abort_at_heading) return 42;
        break;
    case MD_BLOCK_CODE:
        copy_attribute(r->lang, sizeof r->lang, &((MD_BLOCK_CODE_DETAIL *)detail)->lang);
        break;
    case MD_BLOCK_TABLE:
        r->table_columns = ((MD_BLOCK_TABLE_DETAIL *)detail)->col_count;
        break;
    case MD_BLOCK_TD:
        r->last_align = ((MD_BLOCK_TD_DETAIL *)detail)->align;
        break;
    case MD_BLOCK_BLANK:
        r->blank_lines += ((MD_BLOCK_BLANK_DETAIL *)detail)->line_count;
        break;
    default:
        break;
    }
    return 0;
}

static int leave_block(MD_BLOCKTYPE type, void *detail, void *userdata) {
    Record *r = userdata;
    (void)type, (void)detail;
    if (--r->depth < 0) r->unbalanced = 1;
    return 0;
}

static int enter_span(MD_SPANTYPE type, void *detail, void *userdata) {
    Record *r = userdata;
    r->spans[type]++;
    if (type == MD_SPAN_A) copy_attribute(r->href, sizeof r->href, &((MD_SPAN_A_DETAIL *)detail)->href);
    return 0;
}

static int leave_span(MD_SPANTYPE type, void *detail, void *userdata) {
    (void)type, (void)detail, (void)userdata;
    return 0;
}

static int text(MD_TEXTTYPE type, const MD_CHAR *data, MD_SIZE size, void *userdata) {
    Record *r = userdata;
    size_t used = strlen(r->text);
    (void)type;
    if (used + size < sizeof r->text) {
        memcpy(r->text + used, data, size);
        r->text[used + size] = '\0';
    }
    return 0;
}

static int parse(const char *markdown, unsigned flags, Record *r) {
    MD_PARSER parser;
    memset(&parser, 0, sizeof parser);
    parser.flags = flags;
    parser.enter_block = enter_block;
    parser.leave_block = leave_block;
    parser.enter_span = enter_span;
    parser.leave_span = leave_span;
    parser.text = text;
    return md_parse(markdown, (MD_SIZE)strlen(markdown), &parser, r);
}

static void parsing(void) {
    static const char doc[] = "## Notes\n\n"
                              "- see [the site](https://example.com/a?b=c)\n"
                              "- ~~old~~ new\n\n\n"
                              "```c\nint x;\n```\n\n"
                              "| k | v |\n|---|--:|\n| 1 | 2 |\n";
    Record r;
    memset(&r, 0, sizeof r);
    check(parse(doc, MD_DIALECT_GITHUB | MD_FLAG_PRESERVEBLANKLINES, &r) == 0, "md_parse succeeds");
    check(r.blocks[MD_BLOCK_DOC] == 1 && r.blocks[MD_BLOCK_H] == 1 && r.heading_level == 2, "the heading level");
    check(r.blocks[MD_BLOCK_UL] == 1 && r.blocks[MD_BLOCK_LI] == 2, "the list and its items");
    check(r.spans[MD_SPAN_A] == 1 && strcmp(r.href, "https://example.com/a?b=c") == 0, "the link target");
    check(r.spans[MD_SPAN_DEL] == 1, "strikethrough in the GitHub dialect");
    check(r.blocks[MD_BLOCK_CODE] == 1 && strcmp(r.lang, "c") == 0, "the code block language");
    check(r.blocks[MD_BLOCK_TABLE] == 1 && r.table_columns == 2 && r.blocks[MD_BLOCK_TD] == 2 &&
              r.last_align == MD_ALIGN_RIGHT,
          "the table shape and alignment");
    check(r.blocks[MD_BLOCK_BLANK] == 3 && r.blank_lines == 4, "the blank-line runs");
    check(r.depth == 0 && !r.unbalanced && r.max_depth == 5, "blocks open and close in pairs");
    check(strstr(r.text, "Notes") && strstr(r.text, "the site") && strstr(r.text, "int x;"), "the text");

    memset(&r, 0, sizeof r);
    r.abort_at_heading = 1;
    check(parse("intro\n\n# stop here\n\nnever seen\n", 0, &r) == 42, "a callback aborts with its own value");
    check(r.blocks[MD_BLOCK_P] == 1 && strstr(r.text, "never") == NULL, "nothing is reported after the abort");
}

int main(void) {
    rendering();
    parsing();
    printf("md4c (C): %s\n", ok ? "ok" : "FAILED");
    return ok ? 0 : 1;
}
