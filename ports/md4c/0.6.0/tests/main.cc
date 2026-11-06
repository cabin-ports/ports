// The C++ consumer, in C++98: md4c-html renders footnotes (numbering,
// repeated references, unreferenced definitions), permissive autolinks,
// wiki links, and raw HTML with and without MD_FLAG_NOHTML into a
// std::string; md4c's parser drives a small outline builder that collects
// heading levels and titles while skipping inline markup.
#include <md4c-html.h>
#include <md4c.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

typedef std::vector<std::pair<unsigned, std::string> > Headings;

// Collects (level, title) for every heading, ignoring inline markup.
struct Outline {
    Headings headings;
    bool in_heading;
    Outline() : in_heading(false) {}
};

// The callbacks md4c calls have C language linkage.
extern "C" {
static void append(const MD_CHAR *text, MD_SIZE size, void *userdata) {
    static_cast<std::string *>(userdata)->append(text, size);
}

static int enter_block(MD_BLOCKTYPE type, void *detail, void *userdata) {
    Outline *o = static_cast<Outline *>(userdata);
    if (type == MD_BLOCK_H) {
        o->headings.push_back(std::make_pair(static_cast<MD_BLOCK_H_DETAIL *>(detail)->level, std::string()));
        o->in_heading = true;
    }
    return 0;
}

static int leave_block(MD_BLOCKTYPE type, void *, void *userdata) {
    if (type == MD_BLOCK_H) static_cast<Outline *>(userdata)->in_heading = false;
    return 0;
}

static int span(MD_SPANTYPE, void *, void *) { return 0; }

static int text(MD_TEXTTYPE, const MD_CHAR *data, MD_SIZE size, void *userdata) {
    Outline *o = static_cast<Outline *>(userdata);
    if (o->in_heading) o->headings.back().second.append(data, size);
    return 0;
}
}

static bool ok = true;

static void check(bool pass, const char *what) {
    if (!pass) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ok = false;
    }
}

static void expect(const std::string &markdown, unsigned flags, const std::string &expected, const char *what) {
    std::string got;
    if (md_html(markdown.data(), static_cast<MD_SIZE>(markdown.size()), append, &got, flags, 0) != 0)
        got = "md_html failed";
    check(got == expected, what);
    if (got != expected) std::fprintf(stderr, "  got: %s\n", got.c_str());
}

static void rendering() {
    expect("Cabin[^c] builds C[^lang].\n\n[^lang]: And C++.\n[^c]: A package manager.\n", MD_FLAG_FOOTNOTES,
           "<p>Cabin<sup><a href=\"#fn-1\" id=\"fnref-1-1\">1</a></sup> builds C<sup><a href=\"#fn-2\" "
           "id=\"fnref-2-1\">2</a></sup>.</p>\n<section class=\"footnotes\">\n<ol>\n<li id=\"fn-1\">\n"
           "A package manager.<a href=\"#fnref-1-1\" class=\"footnote-backref\">&#8617;</a>\n</li>\n"
           "<li id=\"fn-2\">\nAnd C++.<a href=\"#fnref-2-1\" class=\"footnote-backref\">&#8617;</a>\n</li>\n"
           "</ol>\n</section>\n",
           "footnotes are numbered in reference order");
    expect("Fast[^p], small[^p].\n\n[^p]: Measured.\n[^x]: Never cited.\n", MD_FLAG_FOOTNOTES,
           "<p>Fast<sup><a href=\"#fn-1\" id=\"fnref-1-1\">1</a></sup>, small<sup><a href=\"#fn-1\" "
           "id=\"fnref-1-2\">1</a></sup>.</p>\n<section class=\"footnotes\">\n<ol>\n<li id=\"fn-1\">\n"
           "Measured.<a href=\"#fnref-1-1\" class=\"footnote-backref\">&#8617;</a> <a href=\"#fnref-1-2\" "
           "class=\"footnote-backref\">&#8617;</a>\n</li>\n</ol>\n</section>\n",
           "a twice-cited footnote links back twice and an uncited one is dropped");
    expect("Mail team@example.org, see https://example.org/docs or www.example.org\n", MD_FLAG_PERMISSIVEAUTOLINKS,
           "<p>Mail <a href=\"mailto:team@example.org\">team@example.org</a>, see "
           "<a href=\"https://example.org/docs\">https://example.org/docs</a> or "
           "<a href=\"http://www.example.org\">www.example.org</a></p>\n",
           "permissive autolinks");
    expect("https://example.org/docs\n", 0, "<p>https://example.org/docs</p>\n", "autolinks need their flags");
    expect("[[Home page]]\n", MD_FLAG_WIKILINKS,
           "<p><x-wikilink data-target=\"Home page\">Home page</x-wikilink></p>\n", "a wiki link");
    expect("a <b>bold</b> move\n\n<div>x</div>\n", 0, "<p>a <b>bold</b> move</p>\n<div>x</div>\n",
           "raw HTML passes through");
    expect("a <b>bold</b> move\n\n<div>x</div>\n", MD_FLAG_NOHTML,
           "<p>a &lt;b&gt;bold&lt;/b&gt; move</p>\n<p>&lt;div&gt;x&lt;/div&gt;</p>\n",
           "MD_FLAG_NOHTML escapes inline and block HTML");
}

static Headings outline(const std::string &markdown) {
    MD_PARSER parser;
    std::memset(&parser, 0, sizeof parser);
    parser.flags = MD_DIALECT_GITHUB;
    parser.enter_block = enter_block;
    parser.leave_block = leave_block;
    parser.enter_span = span;
    parser.leave_span = span;
    parser.text = text;
    Outline o;
    if (md_parse(markdown.data(), static_cast<MD_SIZE>(markdown.size()), &parser, &o) != 0) return Headings();
    return o.headings;
}

static void parsing() {
    const Headings headings = outline("Intro\n=====\n\ntext\n\n## Using *md4c*\n\n```\n# not a heading\n```\n\n"
                                      "### Tables `|` and ~~lists~~\n\n#### \n");
    Headings expected;
    expected.push_back(std::make_pair(1u, std::string("Intro")));
    expected.push_back(std::make_pair(2u, std::string("Using md4c")));
    expected.push_back(std::make_pair(3u, std::string("Tables | and lists")));
    expected.push_back(std::make_pair(4u, std::string()));
    check(headings == expected, "the outline of setext and ATX headings");
}

int main() {
    rendering();
    parsing();
    std::printf("md4c (C++): %s\n", ok ? "ok" : "FAILED");
    return ok ? 0 : 1;
}
