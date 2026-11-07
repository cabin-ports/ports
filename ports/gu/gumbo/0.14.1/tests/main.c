/* gumbo parses HTML5 into a tree: a small document with its doctype,
 * attributes, character references, void elements, a comment, and an implied
 * <tbody>; misnested formatting repaired by the adoption agency; quirks
 * modes; UTF-8 decoding with a replacement character; foreign (SVG) content;
 * fragment parsing; the error limit; a counting allocator; and the tag
 * helpers.  C99. */
#include <gumbo.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

static void check(int ok, int line, const char *what)
{
    if (!ok) {
        fprintf(stderr, "main.c:%d: check failed: %s\n", line, what);
        ++failures;
    }
}

#define CHECK(expr) check((expr) != 0, __LINE__, #expr)

/* The nth element child of node (counting from 0) with the given tag, or
 * NULL. */
static GumboNode *child(const GumboNode *node, GumboTag tag, int nth)
{
    const GumboVector *children;
    unsigned int i;
    if (!node || node->type != GUMBO_NODE_ELEMENT)
        return NULL;
    children = &node->v.element.children;
    for (i = 0; i < children->length; ++i) {
        GumboNode *c = children->data[i];
        if (c->type == GUMBO_NODE_ELEMENT && c->v.element.tag == tag &&
            nth-- == 0)
            return c;
    }
    return NULL;
}

/* The text of the ith child of node, if that child is a text node. */
static const char *text_at(const GumboNode *node, unsigned int i)
{
    const GumboNode *c;
    if (!node || node->type != GUMBO_NODE_ELEMENT ||
        i >= node->v.element.children.length)
        return "<none>";
    c = node->v.element.children.data[i];
    return c->type == GUMBO_NODE_TEXT ? c->v.text.text : "<not text>";
}

static const char *attr(const GumboNode *node, const char *name)
{
    GumboAttribute *a;
    if (!node)
        return "<none>";
    a = gumbo_get_attribute(&node->v.element.attributes, name);
    return a ? a->value : "<none>";
}

static const char page[] =
    "<!DOCTYPE html>\n"
    "<html lang=\"en\"><head><title>Cabin &amp; gumbo</title></head>\n"
    "<body><p class=\"a b\" id=x>Hello, <b>world</b>&excl;<br>"
    "<img src=i.png alt></p>\n"
    "<!-- note --><table><tr><td>1</table></body></html>";

static void test_document(void)
{
    GumboOutput *out = gumbo_parse(page);
    const GumboDocument *doc = &out->document->v.document;
    GumboNode *html = out->root, *head, *body, *p, *img, *table, *tbody, *td;
    GumboAttribute *id;
    GumboStringPiece tag;
    unsigned int i;
    int comments = 0;

    CHECK(out->document->type == GUMBO_NODE_DOCUMENT);
    CHECK(doc->has_doctype && strcmp(doc->name, "html") == 0);
    CHECK(doc->doc_type_quirks_mode == GUMBO_DOCTYPE_NO_QUIRKS);
    CHECK(out->errors.length == 0);

    CHECK(html->type == GUMBO_NODE_ELEMENT &&
          html->v.element.tag == GUMBO_TAG_HTML);
    CHECK(strcmp(attr(html, "lang"), "en") == 0);
    head = child(html, GUMBO_TAG_HEAD, 0);
    CHECK(strcmp(text_at(child(head, GUMBO_TAG_TITLE, 0), 0),
                 "Cabin & gumbo") == 0);

    body = child(html, GUMBO_TAG_BODY, 0);
    p = child(body, GUMBO_TAG_P, 0);
    CHECK(p != NULL);
    if (!p) {
        gumbo_destroy_output(&kGumboDefaultOptions, out);
        return;
    }
    CHECK(strcmp(attr(p, "class"), "a b") == 0);
    id = gumbo_get_attribute(&p->v.element.attributes, "id");
    CHECK(id && strcmp(id->value, "x") == 0 && id->original_value.length == 1);
    /* Positions are 1-based lines and columns and 0-based byte offsets. */
    CHECK(p->v.element.start_pos.line == 3);
    CHECK(p->v.element.start_pos.column == 7);
    CHECK(p->v.element.start_pos.offset ==
          (unsigned int)(strstr(page, "<p ") - page));
    tag = p->v.element.original_tag;
    gumbo_tag_from_original_text(&tag);
    CHECK(tag.length == 1 && tag.data[0] == 'p');

    /* "Hello, ", <b>world</b>, "!" from &excl;, <br>, <img>. */
    CHECK(p->v.element.children.length == 5);
    CHECK(strcmp(text_at(p, 0), "Hello, ") == 0);
    CHECK(strcmp(text_at(child(p, GUMBO_TAG_B, 0), 0), "world") == 0);
    CHECK(strcmp(text_at(p, 2), "!") == 0);
    CHECK(child(p, GUMBO_TAG_BR, 0) != NULL);
    img = child(p, GUMBO_TAG_IMG, 0);
    CHECK(strcmp(attr(img, "src"), "i.png") == 0);
    CHECK(strcmp(attr(img, "alt"), "") == 0);

    for (i = 0; i < body->v.element.children.length; ++i) {
        const GumboNode *c = body->v.element.children.data[i];
        if (c->type == GUMBO_NODE_COMMENT) {
            ++comments;
            CHECK(strcmp(c->v.text.text, " note ") == 0);
        }
    }
    CHECK(comments == 1);

    /* The parser inserts the <tbody> the markup leaves out. */
    table = child(body, GUMBO_TAG_TABLE, 0);
    tbody = child(table, GUMBO_TAG_TBODY, 0);
    CHECK(tbody && (tbody->parse_flags & GUMBO_INSERTION_IMPLIED));
    td = child(child(tbody, GUMBO_TAG_TR, 0), GUMBO_TAG_TD, 0);
    CHECK(strcmp(text_at(td, 0), "1") == 0);
    CHECK(td && td->parent->parent == tbody);

    gumbo_destroy_output(&kGumboDefaultOptions, out);
}

static const char misnested[] = "<p><b>1<i>2</b>3</i></p>";

static void test_recovery(void)
{
    GumboOutput *out = gumbo_parse(misnested);
    GumboNode *body = child(out->root, GUMBO_TAG_BODY, 0);
    GumboNode *p = child(body, GUMBO_TAG_P, 0);
    GumboNode *b = child(p, GUMBO_TAG_B, 0);
    GumboNode *i2 = child(p, GUMBO_TAG_I, 0);

    /* No doctype, and </b> closes an open <i>: both are parse errors. */
    CHECK(out->errors.length >= 2);
    CHECK(out->document->v.document.doc_type_quirks_mode ==
          GUMBO_DOCTYPE_QUIRKS);
    /* <p><b>1<i>2</i></b><i>3</i></p>: the <i> is reopened after </b>. */
    CHECK(p && p->v.element.children.length == 2);
    CHECK(strcmp(text_at(b, 0), "1") == 0);
    CHECK(strcmp(text_at(child(b, GUMBO_TAG_I, 0), 0), "2") == 0);
    CHECK(i2 && (i2->parse_flags &
                 GUMBO_INSERTION_RECONSTRUCTED_FORMATTING_ELEMENT));
    CHECK(strcmp(text_at(i2, 0), "3") == 0);
    gumbo_destroy_output(&kGumboDefaultOptions, out);

    out = gumbo_parse(
        "<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.0 Transitional//EN\" "
        "\"http://www.w3.org/TR/xhtml1/DTD/xhtml1-transitional.dtd\"><p>x");
    CHECK(out->document->v.document.doc_type_quirks_mode ==
          GUMBO_DOCTYPE_LIMITED_QUIRKS);
    CHECK(strcmp(out->document->v.document.public_identifier,
                 "-//W3C//DTD XHTML 1.0 Transitional//EN") == 0);
    gumbo_destroy_output(&kGumboDefaultOptions, out);
}

static void test_text_and_foreign(void)
{
    /* A two-byte UTF-8 sequence, a hexadecimal and a decimal numeric
     * reference, and an invalid byte, which becomes U+FFFD. */
    GumboOutput *out = gumbo_parse("caf\xc3\xa9 &#x41;&#128512; \xff"
                                   "<svg viewbox=\"0 0 1 1\"><circle/></svg>");
    GumboNode *body = child(out->root, GUMBO_TAG_BODY, 0);
    GumboNode *svg = child(body, GUMBO_TAG_SVG, 0);
    GumboAttribute *viewbox =
        svg ? gumbo_get_attribute(&svg->v.element.attributes, "viewbox")
            : NULL;

    CHECK(strcmp(text_at(body, 0),
                 "caf\xc3\xa9 A\xf0\x9f\x98\x80 \xef\xbf\xbd") == 0);
    /* In SVG the attribute name takes its camel-case form (the lookup itself
     * ignores case). */
    CHECK(svg && svg->v.element.tag_namespace == GUMBO_NAMESPACE_SVG);
    CHECK(viewbox && strcmp(viewbox->name, "viewBox") == 0 &&
          strcmp(viewbox->value, "0 0 1 1") == 0);
    gumbo_destroy_output(&kGumboDefaultOptions, out);
}

struct counts {
    long allocs, frees;
};

static void *count_alloc(void *userdata, size_t size)
{
    ((struct counts *)userdata)->allocs++;
    return malloc(size);
}

static void count_free(void *userdata, void *ptr)
{
    if (ptr)
        ((struct counts *)userdata)->frees++;
    free(ptr);
}

static void test_options(void)
{
    GumboOptions options = kGumboDefaultOptions;
    struct counts n = {0, 0};
    GumboOutput *out;

    /* A <title> context makes the fragment RCDATA: the tag stays text. */
    options.fragment_context = GUMBO_TAG_TITLE;
    out = gumbo_parse_with_options(&options, "a<b>c", 5);
    CHECK(out->root->v.element.children.length == 1);
    CHECK(strcmp(text_at(out->root, 0), "a<b>c") == 0);
    gumbo_destroy_output(&options, out);

    options = kGumboDefaultOptions;
    options.max_errors = 1;
    out = gumbo_parse_with_options(&options, misnested, strlen(misnested));
    CHECK(out->errors.length == 1);
    gumbo_destroy_output(&options, out);

    options = kGumboDefaultOptions;
    options.allocator = count_alloc;
    options.deallocator = count_free;
    options.userdata = &n;
    out = gumbo_parse_with_options(&options, page, strlen(page));
    CHECK(n.allocs > 0 && n.frees < n.allocs);
    gumbo_destroy_output(&options, out);
    CHECK(n.frees == n.allocs);
}

static void test_tags(void)
{
    GumboStringPiece fo = {"foreignobject", 13};
    GumboStringPiece upper = {"FOREIGNOBJECT", 13};

    CHECK(gumbo_tag_enum("TaBlE") == GUMBO_TAG_TABLE);
    CHECK(gumbo_tagn_enum("divx", 3) == GUMBO_TAG_DIV);
    CHECK(gumbo_tag_enum("nosuchtag") == GUMBO_TAG_UNKNOWN);
    CHECK(strcmp(gumbo_normalized_tagname(GUMBO_TAG_SELECTEDCONTENT),
                 "selectedcontent") == 0);
    CHECK(gumbo_tag_is_void(GUMBO_TAG_IMG) && !gumbo_tag_is_void(GUMBO_TAG_DIV));
    CHECK(strcmp(gumbo_normalize_svg_tagname(&fo), "foreignObject") == 0);
    CHECK(gumbo_string_equals_ignore_case(&fo, &upper));
    CHECK(!gumbo_string_equals(&fo, &upper));
}

int main(void)
{
    test_document();
    test_recovery();
    test_text_and_foreign();
    test_options();
    test_tags();
    if (failures != 0)
        return 1;
    puts("gumbo 0.14.1: C API OK");
    return 0;
}
