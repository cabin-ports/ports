// Consumer test for cabin-ports/inja: Jinja-style templates rendered from
// nlohmann::json data, with inja's statements, functions, callbacks,
// in-memory includes and inheritance, whitespace control, and errors.
#include <inja/inja.hpp>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <sstream>
#include <string>
#include <system_error>
#include <type_traits>

static_assert(std::is_same<inja::json, nlohmann::json>::value,
              "inja renders nlohmann::json data");

namespace {

int failures = 0;

void check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAILED: %s\n", what);
    ++failures;
  }
}

template <typename E, typename F>
std::string error_of(F&& f) {
  try {
    f();
  } catch (const E& e) {
    return e.what();
  }
  return "no exception";
}

inja::json sample() {
  inja::json data;
  data["name"] = "Peter";
  data["guests"] = {"Jeff", "Tom", "Patrick"};
  data["time"]["start"] = 16;
  data["time"]["end"] = 22;
  data["hour"] = 19;
  data["tired"] = true;
  return data;
}

// The README's tutorial and upstream's tests.
void test_render() {
  const inja::json data = sample();
  check(inja::render("Hello {{ name }}!", data) == "Hello Peter!",
        "render substitutes a variable");
  check(inja::render("{{ guests.1 }}", data) == "Tom", "an array index");
  check(inja::render("{{ time.start }} to {{ time.end + 1 }}pm", data) ==
            "16 to 23pm",
        "nested objects and arithmetic");
  check(inja::render("Hello{# a comment #}!", data) == "Hello!",
        "comments render nothing");

  std::ostringstream os;
  inja::render_to(os, "{{ length(guests) }} guests", data);
  check(os.str() == "3 guests", "render_to writes to a stream");

  inja::Environment env;
  const inja::Template tmpl = env.parse("Hi {{ name }}");
  inja::json other = data;
  other["name"] = "Inja";
  check(env.render(tmpl, data) == "Hi Peter" &&
            env.render(tmpl, other) == "Hi Inja",
        "a parsed template renders again with other data");
}

void test_statements() {
  const inja::json data = sample();
  check(inja::render("{% for g in guests %}{{ loop.index1 }}:{{ g }}"
                     "{% if not loop.is_last %}, {% endif %}{% endfor %}",
                     data) == "1:Jeff, 2:Tom, 3:Patrick",
        "a loop with loop.index1 and loop.is_last");
  check(inja::render("{% for k, v in time %}{{ k }}={{ v }};{% endfor %}",
                     data) == "end=22;start=16;",
        "a loop over an object's sorted keys");
  check(inja::render("Guests:\n## for g in guests\n- {{ g }}\n## endfor\n",
                     data) == "Guests:\n- Jeff\n- Tom\n- Patrick\n",
        "line statements");
  check(inja::render("{% if hour >= 20 %}Serve{% else if hour >= 18 %}Make"
                     "{% else %}Plan{% endif %} dinner.",
                     data) == "Make dinner.",
        "if, else if and else");
  check(inja::render("{% if name in guests %}in{% else %}out{% endif %}",
                     data) == "out",
        "the in operator");
  check(inja::render("{% if length(guests) < (3+2) and tired %}Sleepy"
                     "{% endif %}",
                     data) == "Sleepy",
        "logical operators");
  check(inja::render("{% set hour=23 %}{{ hour }}pm, {% set time.start=18 %}"
                     "{{ time.start }}",
                     data) == "23pm, 18",
        "set assigns names and JSON pointers");
  inja::Environment env;
  const inja::Template tmpl = env.parse("{{ hour }}{% set hour=23 %}{{ hour }}");
  check(env.render(tmpl, data) == "1923" && env.render(tmpl, data) == "1923",
        "set is local to one rendering");
}

void test_functions() {
  const inja::json data = sample();
  check(inja::render("{{ upper(name) }} {{ lower(name) }} "
                     "{{ capitalize(\"pETER\") }}",
                     data) == "PETER peter Peter",
        "upper, lower and capitalize");
  check(inja::render("{{ replace(name, \"e\", \"3\") }}", data) == "P3t3r",
        "replace");
  check(inja::render("{% for i in range(3) %}{{ at(guests, i) }} "
                     "{% endfor %}",
                     data) == "Jeff Tom Patrick ",
        "range and at");
  check(inja::render("{{ first(guests) }}/{{ last(guests) }}", data) ==
            "Jeff/Patrick",
        "first and last");
  check(inja::render("{{ sort([3, 2, 1]) }}", data) == "[1,2,3]",
        "sort renders a JSON array");
  check(inja::render("{{ join(sort(guests), \", \") }}", data) ==
            "Jeff, Patrick, Tom",
        "join");
  check(inja::render("{{ round(3.1415, 0) }} {{ round(3.1415, 3) }}", data) ==
            "3 3.142",
        "round (an integer at precision 0)");
  check(inja::render("{{ odd(42) }} {{ even(42) }} {{ divisibleBy(42, 7) }}",
                     data) == "false true true",
        "odd, even and divisibleBy");
  check(inja::render("{{ max([1, 2, 3]) }} {{ min([-2.4, -1.2, 4.5]) }}",
                     data) == "3 -2.4",
        "max and min");
  check(inja::render("{{ int(\"2\") == 2 }} {{ float(\"1.8\") > 2 }}", data) ==
            "true false",
        "int and float");
  check(inja::render("{{ default(name, \"x\") }} {{ default(nobody, \"x\") }}",
                     data) == "Peter x",
        "default");
  check(inja::render("{{ exists(\"guests\") }} {{ exists(\"city\") }} "
                     "{{ existsIn(time, \"start\") }}",
                     data) == "true false true",
        "exists and existsIn");
  check(inja::render("{{ isString(name) }} {{ isArray(name) }}", data) ==
            "true false",
        "type checks");
  check(inja::render("{{ [\"B\", \"A\", \"C\"] | sort | join(\",\") }}",
                     data) == "A,B,C",
        "the pipe syntax");
}

void test_environment() {
  const inja::json data = sample();
  inja::Environment env;

  env.add_callback("double", 1, [](inja::Arguments& args) {
    return 2 * args.at(0)->get<int>();
  });
  env.add_callback("argmax", [](inja::Arguments& args) {
    const auto it = std::max_element(
        args.begin(), args.end(),
        [](const inja::json* a, const inja::json* b) { return *a < *b; });
    return std::distance(args.begin(), it);
  });
  env.add_callback("greeting", 0, [](inja::Arguments&) {
    return std::string("Hello Hello!");
  });
  int logged = 0;
  env.add_void_callback("log", 1, [&logged](inja::Arguments&) { ++logged; });
  check(env.render("{{ double(16) }}", data) == "32", "a callback");
  check(env.render("{{ argmax(4, 2, 6) }} {{ argmax(0, 2, 6, 8, 3) }}",
                   data) == "2 3",
        "a variadic callback");
  check(env.render("{{ greeting }}", data) == "Hello Hello!",
        "a callback without arguments works as a variable");
  check(env.render("[{{ log(name) }}]", data) == "[]" && logged == 1,
        "a void callback renders nothing");

  env.include_template("content", env.parse("Hello {{ name }}!"));
  check(env.render("Content: {% include \"content\" %}", data) ==
            "Content: Hello Peter!",
        "an in-memory include");

  env.set_search_included_templates_in_files(false);
  env.set_include_callback(
      [&env](const std::filesystem::path&, const std::string& name) {
        return env.parse("from " + name);
      });
  check(env.render("{% include \"footer\" %}", data) == "from footer",
        "the include callback supplies a missing template");

  env.include_template("base",
                       env.parse("<{% block title %}Default{% endblock %}|"
                                 "{% block body %}base{% endblock %}>"));
  check(env.render("{% extends \"base\" %}"
                   "{% block title %}{{ super() }} + Child{% endblock %}"
                   "{% block body %}{{ name }}{% endblock %}",
                   data) == "<Default + Child|Peter>",
        "extends, block and super");

  inja::Environment ws;
  check(ws.render("   {{- name -}}   !", data) == "Peter!",
        "- strips whitespace by hand");
  // A leading - strips only a line that is all whitespace up to the block,
  // so the README's "Hello       {{- name -}}     !" keeps the spaces after
  // "Hello".
  check(ws.render("Hello   {{- name -}}   !", data) == "Hello   Peter!",
        "- keeps whitespace after other text on the line");
  ws.set_trim_blocks(true);
  ws.set_lstrip_blocks(true);
  check(ws.render("  {% if tired %}\nyes\n  {% endif %}\n.", data) == "yes\n.",
        "trim_blocks and lstrip_blocks");

  inja::Environment custom;
  custom.set_expression("<%", "%>");
  custom.set_statement("[[", "]]");
  check(custom.render("[[ if tired ]]<% name %>[[ endif ]] {{ name }}", data) ==
            "Peter {{ name }}",
        "custom delimiters");

  inja::Environment html;
  html.set_html_autoescape(true);
  inja::json markup;
  markup["text"] = "<a href=\"x\">&'</a>";
  check(html.render("{{ text }}", markup) ==
            "&lt;a href=&quot;x&quot;&gt;&amp;&apos;&lt;/a&gt;",
        "HTML autoescaping");
}

// The file API: an Environment rooted at a directory reads templates and
// JSON from it, resolves includes relative to the including file, and
// writes results there.
void test_files() {
  namespace fs = std::filesystem;
  // A directory of this run's own: create_directory() is false if taken.
  std::random_device entropy;
  fs::path dir;
  do {
    dir = fs::temp_directory_path() /
          ("cabin-inja-test-" + std::to_string(entropy()));
  } while (!fs::create_directory(dir));
  fs::create_directory(dir / "parts");
  std::ofstream(dir / "page.txt") << "{% include \"parts/head.txt\" %}{{ name }}";
  std::ofstream(dir / "parts" / "head.txt") << "Dear ";
  std::ofstream(dir / "data.json") << "{\"name\": \"Peter\"}";

  inja::Environment env{dir};
  const inja::json data = sample();
  check(env.render_file("page.txt", data) == "Dear Peter",
        "render_file includes a file relative to the template");
  check(env.render_file_with_json_file("page.txt", "data.json") ==
            "Dear Peter",
        "render_file_with_json_file reads the data file");
  env.write(env.parse_template("page.txt"), data, "out.txt");
  std::ifstream out(dir / "out.txt");
  const std::string written((std::istreambuf_iterator<char>(out)),
                            std::istreambuf_iterator<char>());
  check(written == "Dear Peter", "write renders into a file");
  check(error_of<inja::FileError>([&] { env.render_file("missing.txt", data); })
                .rfind("[inja.exception.file_error] failed accessing file at '",
                       0) == 0,
        "a missing template throws a FileError");
  out.close();
  std::error_code ec;
  fs::remove_all(dir, ec);
}

void test_errors() {
  const inja::json data = sample();
  check(error_of<inja::RenderError>([&] { inja::render("{{unknown}}", data); }) ==
            "[inja.exception.render_error] (at 1:3) variable 'unknown' not "
            "found",
        "a missing variable throws a RenderError with its location");
  check(error_of<inja::ParserError>([&] { inja::render("{{", data); }) ==
            "[inja.exception.parser_error] (at 1:3) expected expression "
            "close, got '<eof>'",
        "an unterminated expression throws a ParserError");
  check(error_of<inja::ParserError>([&] {
          inja::render("line\n{% for x ins guests %}{% endfor %}", data);
        }) == "[inja.exception.parser_error] (at 2:10) expected 'in', got "
              "'ins'",
        "errors report the line and column in the template");
}

}  // namespace

int main() {
  test_render();
  test_statements();
  test_functions();
  test_environment();
  test_files();
  test_errors();
  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("inja consumer ok");
  return 0;
}
