#include <cstdio>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include <bustache/render/ostream.hpp>
#include <bustache/render/string.hpp>

namespace {

int failures = 0;

void expect(std::string const& got, std::string const& want, char const* what)
{
    if (got != want)
    {
        std::fprintf(stderr, "FAIL %s: got [%s], want [%s]\n", what, got.c_str(), want.c_str());
        ++failures;
    }
}

struct value;

struct object : std::vector<std::pair<std::string, value>>
{
    using vector::vector;
    using mapped_type = value;
    const_iterator find(std::string const& key) const;
};

using array = std::vector<value>;

struct value : std::variant<bool, int, std::string, object, array>
{
    using variant::variant;
    value(char const* s) : variant(std::string(s)) {}
};

object::const_iterator object::find(std::string const& key) const
{
    auto it = begin();
    while (it != end() && it->first != key)
        ++it;
    return it;
}

}

template<>
struct bustache::impl_compatible<value>
{
    static value_ptr get_value_ptr(value::variant const& self)
    {
        return std::visit([](auto const& v) { return value_ptr(&v); }, self);
    }
};

namespace {

struct partials : std::unordered_map<std::string, bustache::format>
{
    using unordered_map::unordered_map;

    bustache::format const* operator()(std::string const& key) const
    {
        auto it = find(key);
        return it == end() ? nullptr : &it->second;
    }
};

std::string render(std::string const& tpl, value const& data, bool html = false)
{
    bustache::format fmt(tpl);
    if (html)
        return bustache::to_string(fmt(data).escape(bustache::escape_html));
    return bustache::to_string(fmt(data));
}

}

int main()
{
    // The README example: a plain string map as the model.
    {
        bustache::format fmt{"{{mustache}} templating"};
        std::unordered_map<std::string, std::string> data{{"mustache", "bustache"}};
        expect(bustache::to_string(fmt(data)), "bustache templating", "quick example");
    }

    // Variables, HTML escaping, unescaped forms and comments.
    {
        object data{{"v", "<a href=\"x\">&</a>"}, {"n", 42}};
        expect(render("{{v}}|{{&v}}|{{{v}}}", data), "<a href=\"x\">&</a>|<a href=\"x\">&</a>|<a href=\"x\">&</a>", "no escape");
        expect(render("{{v}}|{{&v}}|{{{v}}}", data, true),
               "&lt;a href=&quot;x&quot;&gt;&amp;&lt;/a&gt;|<a href=\"x\">&</a>|<a href=\"x\">&</a>", "html escape");
        expect(render("a{{! hidden }}b{{missing}}c", data), "abc", "comment, unresolved");
        expect(render("{{n}}", data), "42", "int");
    }

    // Format specifiers go through std::format.
    {
        object data{{"n", 42}};
        expect(render("[{{n:*^10}}]", data), "[****42****]", "format spec");
    }

    // Sections: list, object context, inverted, falsy, dotted names.
    {
        object data{
            {"items", array{object{{"name", "a"}}, object{{"name", "b"}}, object{{"name", "c"}}}},
            {"on", true},
            {"off", false},
            {"empty", array{}},
            {"who", object{{"name", "me"}}},
            {"a", object{{"b", object{{"c", "deep"}}}}},
        };
        expect(render("{{#items}}<{{name}}>{{/items}}", data), "<a><b><c>", "list section");
        expect(render("{{#on}}yes{{/on}}{{#off}}no{{/off}}", data), "yes", "bool section");
        expect(render("{{#who}}{{name}}{{/who}}", data), "me", "object section");
        expect(render("{{^empty}}none{{/empty}}{{^items}}x{{/items}}{{^off}}!{{/off}}", data), "none!", "inverted");
        expect(render("{{a.b.c}}", data), "deep", "dotted name");
        expect(render("{{#items}}{{name}}{{#on}}+{{/on}} {{/items}}", data), "a+ b+ c+ ", "nested");
    }

    // Standalone section tags consume their line.
    expect(render("x\n{{#on}}\ny\n{{/on}}\nz\n", object{{"on", true}}), "x\ny\nz\n", "standalone lines");

    // Set delimiter.
    expect(render("{{=<% %>=}}<%v%> {{v}}", object{{"v", "1"}}), "1 {{v}}", "set delimiter");

    // Partials come from the context.
    {
        bustache::format fmt("{{#items}}{{>row}}{{/items}}");
        partials ctx{{"row", bustache::format("[{{name}}]")}};
        object data{{"items", array{object{{"name", "x"}}, object{{"name", "y"}}}}};
        expect(bustache::to_string(fmt(data).context(ctx)), "[x][y]", "partial");
    }

    // Lambdas: a lazy value.
    {
        auto lazy = [](bustache::ast::view const*) { return std::string("lazy"); };
        bustache::format fmt("{{v}}");
        std::unordered_map<std::string, decltype(lazy)> data{{"v", lazy}};
        expect(bustache::to_string(fmt(data)), "lazy", "lazy value");
    }

    // Stream output.
    {
        bustache::format fmt("{{greeting}}, {{who}}!");
        std::unordered_map<std::string, std::string> data{{"greeting", "hello"}, {"who", "<w>"}};
        std::ostringstream out;
        out << fmt(data).escape(bustache::escape_html);
        expect(out.str(), "hello, &lt;w&gt;!", "ostream");
    }

    // Parse errors report a code and a position.
    {
        bool section = false, badkey = false;
        try
        {
            bustache::format("{{#a}}x{{/b}}");
        }
        catch (bustache::format_error const& e)
        {
            section = e.code() == bustache::error_section;
        }
        try
        {
            bustache::format("ab{{");
        }
        catch (bustache::format_error const& e)
        {
            badkey = e.code() == bustache::error_badkey;
        }
        expect(section ? "ok" : "bad", "ok", "mismatched section error");
        expect(badkey ? "ok" : "bad", "ok", "unterminated tag error");
    }

    return failures == 0 ? 0 : 1;
}
