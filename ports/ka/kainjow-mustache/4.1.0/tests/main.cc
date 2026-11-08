// The C++ consumer: kainjow-mustache in C++11.
#include <mustache.hpp>

#include <cstdio>
#include <sstream>
#include <string>
#include <utility>

using namespace kainjow::mustache;

static bool ok = true;

static void check(bool pass, const char *what) {
    if (!pass) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ok = false;
    }
}

static std::string render(const std::string &text, const data &values) {
    mustache tmpl{text};
    const std::string out = tmpl.render(values);
    if (!tmpl.is_valid()) {
        std::fprintf(stderr, "FAIL: \"%s\" is invalid: %s\n", text.c_str(), tmpl.error_message().c_str());
        ok = false;
    }
    return out;
}

// The template's error message after rendering it, or "" if it is valid.
static std::string error(const std::string &text, const data &values, std::string *out = nullptr) {
    mustache tmpl{text};
    const std::string result = tmpl.render(values);
    if (out)
        *out = result;
    return tmpl.is_valid() ? std::string() : tmpl.error_message();
}

static void variables() {
    check(render("Hello {{what}}!", data{"what", "World"}) == "Hello World!", "a variable");
    const data markup{"v", "& < > \" '"};
    check(render("{{v}}", markup) == "&amp; &lt; &gt; &quot; &apos;", "HTML escaping");
    check(render("{{{v}}}|{{&v}}", markup) == "& < > \" '|& < > \" '", "unescaped variables");
    check(render("{{ what }}", data{"what", "spaced"}) == "spaced", "spaces inside a tag");

    data values;
    values.set("object", data{"key", "value"});
    values.set("flag", true);
    check(render("[{{missing}}|{{object}}|{{flag}}]", values) == "[||]", "missing and non-string values");

    data c{"c", "deep"};
    data b{"b", c};
    check(render("{{a.b.c}}", data{"a", b}) == "deep", "a dotted name");

    // The documented fallback: b.c is missing under a, so the outer b.c is used.
    data a;
    a.set("b", data{});
    data outer;
    outer.set("a", a);
    outer.set("b", data{"c", "outer"});
    check(render("{{#a}}{{b.c}}{{/a}}", outer) == "outer", "a dotted name falls back to an outer context");
}

static void sections() {
    data values;
    values.set("yes", true);
    values.set("no", false);
    values.set("empty", data{data::type::list});
    check(render("{{#yes}}Y{{/yes}}{{#no}}N{{/no}}{{#empty}}E{{/empty}}{{#missing}}M{{/missing}}", values) == "Y",
          "sections over booleans, an empty list and a missing name");
    check(render("{{^yes}}Y{{/yes}}{{^no}}N{{/no}}{{^empty}}E{{/empty}}{{^missing}}M{{/missing}}", values) == "NEM",
          "inverted sections");

    data employees{data::type::list};
    employees << data{"name", "Steve"} << data{"name", "Bill"};
    check(render("{{#employees}}{{name}}, {{/employees}}", data{"employees", employees}) == "Steve, Bill, ",
          "a list of objects");

    data letters{data::type::list};
    letters << "x" << "y";
    check(render("{{#letters}}[{{.}}]{{/letters}}", data{"letters", letters}) == "[x][y]", "a list of strings");

    data scoped;
    scoped.set("inner", data{"x", "in"});
    scoped.set("x", "out");
    scoped.set("y", "out");
    check(render("{{x}}{{#inner}}{{x}}-{{y}}{{/inner}}{{x}}", scoped) == "outin-outout",
          "a section's names shadow outer ones and outer names stay visible");

    check(render("a{{!x}}b", data{"!x", "variable"}) == "ab", "a comment");
    check(render("{{=<% %>=}}<% x %>{{x}}<%={{ }}=%>{{x}}", data{"x", "X"}) == "X{{x}}X", "set delimiters");

    const data flag{"boolean", true};
    check(render("|\n| This Is\n{{#boolean}}\n|\n{{/boolean}}\n| A Line", flag) == "|\n| This Is\n|\n| A Line",
          "standalone section lines are removed");
    check(render("|\r\n  {{#boolean}}\r\n{{/boolean}}\r\n|", flag) == "|\r\n|", "standalone CRLF lines");
    check(render("|\n\t{{#boolean}}\nY\n\t{{/boolean}}\n|", flag) == "|\nY\n|", "tab-indented standalone lines");
    // The documented gaps: a skipped section and a standalone comment leave a blank line.
    check(render("|\n{{#missing}}\nN\n{{/missing}}\n|", flag) == "|\n\n|", "a skipped section leaves a line");
    check(render("a\n{{! c }}\nb", data{}) == "a\n\nb", "a standalone comment line stays");
}

static void partials_and_lambdas() {
    data values;
    values.set("text", "P{{v}}");
    values.set("function", partial{[]() { return std::string("F{{v}}"); }});
    values.set("v", "1");
    check(render("{{>text}}{{> function }}", values) == "P1F1", "string and function partials");

    std::string seen;
    data people;
    people.set("name", "<S>");
    people.set("wrap", lambda{[&seen](const std::string &text) {
                   seen = text;
                   return "<b>" + text + "</b>";
               }});
    check(render("{{#wrap}}Hi {{name}}{{/wrap}}", people) == "<b>Hi &lt;S&gt;</b>" && seen == "Hi {{name}}",
          "a section lambda gets the raw text and its result is rendered");

    data angle;
    angle.set("lam", lambda{[](const std::string &) { return std::string("<x>"); }});
    check(render("{{lam}}|{{{lam}}}", angle) == "&lt;x&gt;|<x>", "a variable lambda is escaped");

    people.set("bold", lambda2{[](const std::string &text, const renderer &render_text) {
                   return "<b>" + render_text(text) + "|" + render_text(text, true) + "</b>";
               }});
    check(render("{{#bold}}Hi {{name}}{{/bold}}", people) == "<b>Hi &lt;S&gt;|Hi &amp;lt;S&amp;gt;</b>",
          "a lambda2 renders its text");

    mustache custom{"{{v}}{{#wrap}}{{v}}{{/wrap}}{{>p}}"};
    custom.set_custom_escape([](const std::string &text) { return "[" + text + "]"; });
    data escaped;
    escaped.set("v", "<");
    escaped.set("p", "{{v}}");
    escaped.set("wrap", lambda{[](const std::string &text) { return text; }});
    check(custom.render(escaped) == "[<][<][<]", "a custom escape, also inside a lambda and a partial");
    check(render("{{=<% %>=}}<%#wrap%><%v%><%/wrap%>", escaped) == "&lt;",
          "a lambda's text is rendered with the delimiters in force");
}

// Supplies the "greeting" partial on demand, like upstream's file-backed context.
class on_demand_context : public context<std::string> {
public:
    explicit on_demand_context(const data *values) : context<std::string>(values), greeting_("Hi {{name}}") {}

    const data *get_partial(const std::string &name) const override {
        return name == "greeting" ? &greeting_ : context<std::string>::get_partial(name);
    }

private:
    data greeting_;
};

static void outputs() {
    mustache tmpl{"Hello {{what}}!"};
    const data world{"what", "World"};
    std::ostringstream stream;
    check(&tmpl.render(world, stream) == &stream && stream.str() == "Hello World!", "rendering to a stream");
    std::string pieces;
    int calls = 0;
    tmpl.render(world, [&pieces, &calls](const std::string &piece) {
        pieces += piece;
        ++calls;
    });
    check(pieces == "Hello World!" && calls > 0, "rendering to a handler");

    const data person{"name", "Ada"};
    on_demand_context ctx{&person};
    mustache greeting{"{{>greeting}}!"};
    check(greeting.render(ctx) == "Hi Ada!", "a custom context");
    // The documented gap: rendering with a context does not check is_valid().
    mustache broken{"{{name}}{{#open}}"};
    check(!broken.is_valid() && broken.render(ctx) == "Ada", "a context renders an invalid template");

    mustachew wide{L"Hello {{what}} é!"};
    check(wide.render(dataw{L"what", L"Wörld"}) == L"Hello Wörld é!", "std::wstring");
}

static void data_api() {
    data values;
    check(values.is_object() && values.is_empty_object(), "an empty object");
    values.set("x", "1");
    values.set("x", "2");
    check(values.get("x") && values.get("x")->string_value() == "2" && !values.get("missing") &&
              values.is_non_empty_object(),
          "set replaces a value");
    values["y"] = data{"z"};
    check(values.get("y") && values.get("y")->is_string() && values.get("y")->string_value() == "z", "operator[]");

    data items{data::type::list};
    check(items.is_empty_list(), "an empty list");
    items.push_back("a");
    items << data{true};
    check(items.is_non_empty_list() && items.list_value().size() == 2 && items.list_value()[1].is_true(),
          "push_back and operator<<");
    values.push_back("ignored");
    check(values.is_object() && values.get("x") && values.get("x")->string_value() == "2" && values.get("y") &&
              values.get("y")->string_value() == "z",
          "push_back on an object does nothing");

    check(data{false}.is_false() && data{false}.is_bool() && data{partial{}}.is_partial() &&
              data{lambda{}}.is_lambda() && data{lambda2{}}.is_lambda2(),
          "type predicates");
    data moved_from{"text"};
    const data moved{std::move(moved_from)};
    check(moved.is_string() && moved_from.is_invalid(), "a moved-from value is invalid");
}

static void errors() {
    const data none;
    check(error("{{#a}}x", none) == "Unclosed section \"a\" at 0", "an unclosed section");
    check(error("x{{/a}}", none) == "Unopened section \"a\" at 1", "an unopened section");
    check(error("{{#a}}{{/b}}", none) == "Unclosed section \"a\" at 0", "a mismatched section end");
    std::string out = "unchanged";
    check(error("ab{{c", none, &out) == "Unclosed tag at 2" && out.empty(), "an unclosed tag renders nothing");
    check(error("{{=<% =}}", none) == "Invalid set delimiter tag at 0" &&
              error("{{=a= b=}}", none) == "Invalid set delimiter tag at 0",
          "invalid set-delimiter tags");
    check(error("{{>bad}}", data{"bad", "{{#x}}"}) == "Unclosed section \"x\" at 0", "an invalid partial");

    data values;
    values.set("l2", lambda2{[](const std::string &text, const renderer &) { return text; }});
    check(error("x{{l2}}y", values, &out) == "Lambda with render argument is not allowed for regular variables" &&
              out == "x",
          "a lambda2 variable stops rendering");
    // The documented continuation: the error stops each list item, and the text after the section renders.
    data items{data::type::list};
    items << data{"n", "1"} << data{"n", "2"};
    values.set("items", items);
    check(error("a{{#items}}<{{n}}{{l2}}>{{/items}}c", values, &out) ==
                  "Lambda with render argument is not allowed for regular variables" &&
              out == "a<1<2c",
          "a render-time error stops only its section");
    check(error("{{ok}}", data{"ok", "1"}).empty(), "a valid template has no error");
}

int main() {
    variables();
    sections();
    partials_and_lambdas();
    outputs();
    data_api();
    errors();
    std::printf("kainjow-mustache (C++): %s\n", ok ? "ok" : "FAILED");
    return ok ? 0 : 1;
}
