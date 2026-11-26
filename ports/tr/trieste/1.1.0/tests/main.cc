#include <trieste/driver.h>
#include <trieste/json.h>
#include <trieste/yaml.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

using namespace trieste;

static int failures = 0;

static void check(bool ok, const char* what)
{
  if (!ok)
  {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

static int run_driver(std::vector<std::string> args)
{
  std::vector<char*> argv;
  for (auto& arg : args)
    argv.push_back(arg.data());
  argv.push_back(nullptr);
  Driver driver(json::reader());
  return driver.run(static_cast<int>(args.size()), argv.data());
}

int main(int, char** argv)
{
  // Construction, writing and JSON Pointer selection (parsers/json/README.md).
  Node object = json::object(
    {json::member("key_a_str", "value"),
     json::member("key_b_number", 42),
     json::member("key_c_bool", json::boolean(true)),
     json::member("key_d_null", json::null()),
     json::member("key_e_array", json::array({json::value(1), json::value(2)})),
     json::member(
       "key_f_object",
       json::object({json::member("key", json::value("value"))}))});
  check(
    json::to_string(object) ==
      R"({"key_a_str":"value","key_b_number":42,"key_c_bool":true,"key_d_null":null,"key_e_array":[1,2],"key_f_object":{"key":"value"}})",
    "json::to_string");
  check(
    json::select_number(object, {"/key_e_array/1"}) == 2.0,
    "json::select_number");
  auto a = json::select_string(object, {"/key_a_str"});
  check(a && a->view() == "value", "json::select_string");
  check(json::select(object, {"/key_c_bool"}) == json::True, "json::select");
  check(json::select(object, {"/missing"}) == Error, "json::select missing");

  // Reading and JSON Patch (parsers/json/README.md).
  auto reader = json::reader();
  auto doc =
    reader.synthetic(R"json({"foo": {"bar": {"baz": [{"boo": "net"}]}}})json")
      .read();
  auto patch = reader
                 .synthetic(R"json([
    {"op": "copy", "from": "/foo", "path": "/bak"},
    {"op": "replace", "path": "/foo/bar/baz/0/boo", "value": "qux"}
  ])json")
                 .read();
  check(doc.ok && patch.ok, "json::reader");
  if (doc.ok && patch.ok)
    check(
      json::to_string(json::patch(doc.ast->front(), patch.ast->front())) ==
        R"({"foo":{"bar":{"baz":[{"boo":"qux"}]}},"bak":{"bar":{"baz":[{"boo":"net"}]}}})",
      "json::patch");
  check(!json::reader().synthetic("[1,").read().ok, "json::reader rejects");

  // YAML to JSON, the pipeline of parsers/yaml/yamlc.cc.
  auto y = yaml::reader().synthetic("a: 1\nb: [x, true]\n") >> yaml::to_json();
  check(y.ok, "yaml::to_json");
  if (y.ok)
    check(
      json::to_string(y.ast) == R"({"a":1,"b":["x",true]})",
      "yaml::to_json output");
  check(!yaml::reader().synthetic("a: [1\n").read().ok, "yaml::reader rejects");

  // Driver parses its command line with CLI11; argv[0] must name a file.
  auto dir = std::filesystem::temp_directory_path() /
    ("trieste-probe-" + std::to_string(std::random_device()()));
  if (!std::filesystem::create_directory(dir))
  {
    std::fprintf(stderr, "FAIL: create a temporary directory\n");
    return 1;
  }
  auto in = dir / "trieste-probe-in.json";
  auto out = dir / "trieste-probe-out.trieste";
  std::ofstream(in) << R"({"k": [true, null]})";
  check(
    run_driver({argv[0], "build", in.string(), "-p", "groups", "-o", out.string()}) ==
      0,
    "Driver build");
  std::ifstream dump(out);
  std::string language, pass;
  std::getline(dump, language);
  std::getline(dump, pass);
  dump.close();
  check(language == "json" && pass == "groups", "Driver build output");
  check(
    run_driver({argv[0], "build", in.string(), "-p", "nosuchpass"}) ==
      static_cast<int>(CLI::ExitCodes::ValidationError),
    "Driver rejects an unknown pass");
  std::filesystem::remove_all(dir);

  return failures == 0 ? 0 : 1;
}
