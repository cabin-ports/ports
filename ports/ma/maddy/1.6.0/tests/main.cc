// The C++ consumer: maddy's Markdown-to-HTML parser in C++14.
#include <maddy/parser.h>

#include <cstdio>
#include <memory>
#include <sstream>
#include <string>

static bool ok = true;

static std::shared_ptr<maddy::ParserConfig> config(uint32_t parsers) {
    auto c = std::make_shared<maddy::ParserConfig>();
    c->enabledParsers = parsers;
    return c;
}

// Checks that the parser turns the Markdown into exactly the HTML.
static void expect(const char *what, const std::string &markdown, const std::string &html,
                   std::shared_ptr<maddy::ParserConfig> settings = nullptr) {
    const maddy::Parser parser(settings);
    std::stringstream in(markdown);
    const std::string out = parser.Parse(in);
    if (out != html) {
        std::fprintf(stderr, "FAIL: %s\n  got:      %s\n  expected: %s\n", what, out.c_str(), html.c_str());
        ok = false;
    }
}

static void parses_blocks() {
    expect("headlines", "# One **bold** title\n## Two\n###### Six\n",
           "<h1>One <strong>bold</strong> title</h1><h2>Two</h2><h6>Six</h6>");
    auto plain = std::make_shared<maddy::ParserConfig>();
    plain->isHeadlineInlineParsingEnabled = false;
    expect("a headline without inline parsing", "# One **bold** title\n", "<h1>One **bold** title</h1>", plain);

    expect("a paragraph joins its lines", "First line\nsecond line.\n\nNext paragraph.\n",
           "<p>First line second line. </p><p>Next paragraph. </p>");
    expect("a last line without a newline", "Last words", "<p>Last words </p>");
    // Each line goes through the inline parsers separately; HTML is not escaped.
    expect("inline markup",
           "An *italic*, _emphasized_, **strong**, __bold__ and ~~struck~~ text\nwith `x < y` code.\n",
           "<p>An <i>italic</i>, <em>emphasized</em>, <strong>strong</strong>, <strong>bold</strong> and "
           "<s>struck</s> text with <code>x < y</code> code. </p>");
    expect("repeated markers", "**a** and **b**, _c_ and _d_\n",
           "<p><strong>a</strong> and <strong>b</strong>, <em>c</em> and <em>d</em> </p>");
    expect("inline code keeps its markers", "`**a** _b_ *c* ~~d~~` then **e**\n",
           "<p><code>**a** _b_ *c* ~~d~~</code> then <strong>e</strong> </p>");
    expect("a marker before inline code stays", "**bold** then `code`\n",
           "<p>**bold** then <code>code</code> </p>");
    expect("links and images",
           "[home](http://example.com) [titled](http://example.com/a \"A title\") "
           "![logo](http://example.com/logo.png)\n",
           "<p><a href=\"http://example.com\">home</a> <a href=\"http://example.com/a\" title=\"A title\">titled</a> "
           "<img src=\"http://example.com/logo.png\" alt=\"logo\"/> </p>");
    expect("CR line ends become line breaks", "one\r\ntwo\r\n", "<p>one<br> two<br> </p>");

    expect("a code block keeps its text", "```\nint x = 2 * 3; // **not bold**\n```\n",
           "<pre><code>\nint x = 2 * 3; // **not bold**\n</code></pre>");
    expect("a code block with a language", "```cpp\nreturn 0;\n```\n",
           "<pre class=\"cpp\"><code>\nreturn 0;\n</code></pre>");

    expect("an unordered list", "- dash\n* star\n+ plus\n", "<ul><li>dash</li><li>star</li><li>plus</li></ul>");
    expect("an ordered list", "1. first\n  * sub\n2. second\n10. tenth\n",
           "<ol><li>first<ul><li>sub</li></ul></li><li>second</li><li>tenth</li></ol>");
    expect("nested lists", "* outer\n  1. inner **one**\n  * inner two\n* last\n",
           "<ul><li>outer<ol><li>inner <strong>one</strong></li><li>inner two</li></ol></li><li>last</li></ul>");
    expect("a check list", "- [ ] open\n  - [x] sub\n- [x] done\n",
           "<ul class=\"checklist\"><li><label><input type=\"checkbox\"/> open<ul class=\"checklist\"><li><label>"
           "<input type=\"checkbox\" checked=\"checked\"/> sub</label></li></ul></label></li>"
           "<li><label><input type=\"checkbox\" checked=\"checked\"/> done</label></li></ul>");

    expect("a quote", "> A *quoted*\n>\n> * list\n>\n\n",
           "<blockquote><p>A <i>quoted</i>  </p><ul><li>list </li></ul></blockquote>");
    expect("a horizontal line", "before\n\n---\n\nafter\n", "<p>before </p><hr/><p>after </p>");
    expect("a table",
           "|table>\nFruit|Count|Note\n- | - | -\napples|**3**|red\npears|4|green\n- | - | -\ntotal|7|-\n|<table\n",
           "<table><thead><tr><th>Fruit</th><th>Count</th><th>Note</th></tr></thead><tbody><tr><td>apples</td>"
           "<td><strong>3</strong></td><td>red</td></tr><tr><td>pears</td><td>4</td><td>green</td></tr></tbody>"
           "<tfoot><tr><td>total</td><td>7</td><td>-</td></tr></tfoot></table>");
}

static void configures_parsers() {
    const std::string html = "<div class=\"note\">kept</div>\n";
    expect("HTML is a paragraph by default", html, "<p><div class=\"note\">kept</div> </p>");
    expect("the default config is the same", html, "<p><div class=\"note\">kept</div> </p>",
           std::make_shared<maddy::ParserConfig>());
    expect("the HTML parser passes HTML through", html, "<div class=\"note\">kept</div>",
           config(maddy::types::DEFAULT | maddy::types::HTML_PARSER));

    const std::string latex = "$$x = **a**\n= b$$\n";
    expect("LaTeX is a paragraph by default", latex, "<p>$$x = <strong>a</strong> = b$$ </p>");
    expect("the default config keeps LaTeX off", latex, "<p>$$x = <strong>a</strong> = b$$ </p>",
           std::make_shared<maddy::ParserConfig>());
    expect("the LaTeX parser keeps the block", latex, "$$x = **a**\n= b$$\n", config(maddy::types::ALL));

    expect("a config without emphasis", "_kept_ and **strong**\n", "<p>_kept_ and <strong>strong</strong> </p>",
           config(maddy::types::DEFAULT & ~maddy::types::EMPHASIZED_PARSER));
    expect("a config without the table parser leaves a table as text",
           "|table>\nFruit|Count|Note\n- | - | -\napples|**3**|red\n|<table\n",
           "<p>|table> Fruit|Count|Note - | - | - apples|<strong>3</strong>|red |<table </p>",
           config(maddy::types::DEFAULT & ~maddy::types::TABLE_PARSER));
    expect("only the strong parser", "# Title **x**\n\n* item\n",
           "# Title <strong>x</strong> <br/>* item <br/>", config(maddy::types::STRONG_PARSER));
    expect("no parsers", "# Title **x**\n", "# Title **x** <br/>", config(maddy::types::NONE));
}

int main() {
    if (maddy::Parser::version() != "1.6.0") {
        std::fprintf(stderr, "FAIL: version %s\n", maddy::Parser::version().c_str());
        ok = false;
    }
    parses_blocks();
    configures_parsers();
    std::printf("maddy (C++): %s\n", ok ? "ok" : "FAILED");
    return ok ? 0 : 1;
}
