// A C++11 consumer of inipp (a C++-only library): parse INI text with
// errors, then trailing comments, the default section, interpolation, typed
// extraction, and a generate/parse round trip; a custom Format; and the
// same flow for wchar_t.
#include <inipp.h>

#include <cstdio>
#include <list>
#include <memory>
#include <sstream>
#include <string>

namespace {

int failures = 0;

#define CHECK(cond)                                               \
  do {                                                            \
    if (!(cond)) {                                                \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      ++failures;                                                 \
    }                                                             \
  } while (0)

const char kConfig[] =
    "; a leading comment\n"
    "[DEFAULT]\n"
    "host = example.org\n"
    "port = 80\n"
    "\n"
    "  [server]  \n"
    "port = 8080 ; overrides DEFAULT\n"
    "url = http://${host}:${port}/\n"
    "ratio = 0.25\n"
    "verbose = true\n"
    "name =   alpha beta   \n"
    "port = 9090\n"
    "this line has no assignment\n"
    "[broken\n"
    "[client]\n"
    "server = ${server:url}\n"
    "retries = 3\n";

void test_parse_and_extract() {
  inipp::Ini<char> ini;
  std::istringstream is(kConfig);
  ini.parse(is);

  const std::list<std::string> expected_errors = {
      "port = 9090", "this line has no assignment", "[broken"};
  CHECK(ini.errors == expected_errors);
  CHECK(ini.sections.size() == 3);

  const inipp::Ini<char>::Section& server = ini.sections.at("server");
  CHECK(server.at("port") == "8080 ; overrides DEFAULT");
  CHECK(server.at("name") == "alpha beta");
  int port = -1;
  CHECK(!inipp::get_value(server, "port", port));
  CHECK(port == -1);

  ini.strip_trailing_comments();
  ini.default_section(ini.sections["DEFAULT"]);
  ini.interpolate();

  CHECK(inipp::get_value(server, "port", port));
  CHECK(port == 8080);
  CHECK(server.at("host") == "example.org");
  CHECK(server.at("url") == "http://example.org:8080/");
  double ratio = 0;
  CHECK(inipp::get_value(server, "ratio", ratio));
  CHECK(ratio == 0.25);
  bool verbose = false;
  CHECK(inipp::get_value(server, "verbose", verbose));
  CHECK(verbose);
  std::string name;
  CHECK(inipp::get_value(server, "name", name));
  CHECK(name == "alpha beta");
  int not_a_number = 7;
  CHECK(!inipp::get_value(server, "name", not_a_number));
  CHECK(not_a_number == 7);
  int missing = 5;
  CHECK(!inipp::get_value(server, "missing", missing));
  CHECK(missing == 5);

  const inipp::Ini<char>::Section& client = ini.sections.at("client");
  int client_port = 0;
  int retries = 0;
  CHECK(inipp::get_value(client, "port", client_port));
  CHECK(client_port == 80);
  CHECK(inipp::get_value(client, "retries", retries));
  CHECK(retries == 3);
  CHECK(client.at("server") == "http://example.org:8080/");

  std::ostringstream os;
  ini.generate(os);
  inipp::Ini<char> again;
  std::istringstream back(os.str());
  again.parse(back);
  CHECK(again.errors.empty());
  CHECK(again.sections == ini.sections);
}

void test_cyclic_interpolation_stops() {
  inipp::Ini<char> ini;
  ini.sections["cycle"]["a"] = "<${b}>";
  ini.sections["cycle"]["b"] = "${a}";
  ini.interpolate();
  CHECK(ini.sections["cycle"]["a"].find("${cycle:") != std::string::npos);
}

void test_custom_format() {
  const std::shared_ptr<inipp::Format<char>> format =
      std::make_shared<inipp::Format<char>>('[', ']', '=', '#', '%', '(', '.',
                                            ')');
  inipp::Ini<char> ini(format);
  std::istringstream is(
      "# a comment in this format\n"
      "[a]\n"
      "x = 1\n"
      "y = %(x)2\n"
      "[b]\n"
      "z = %(a.y)!\n"
      "; no longer a comment\n");
  ini.parse(is);
  ini.interpolate();
  const std::list<std::string> expected_errors = {"; no longer a comment"};
  CHECK(ini.errors == expected_errors);
  CHECK(ini.sections.at("a").at("y") == "12");
  CHECK(ini.sections.at("b").at("z") == "12!");

  std::ostringstream os;
  ini.generate(os);
  CHECK(os.str() == "[a]\nx=1\ny=12\n\n[b]\nz=12!\n\n");
}

void test_wide() {
  inipp::Ini<wchar_t> ini;
  std::wistringstream is(
      L"[DEFAULT]\n"
      L"unit = kg\n"
      L"[w]\n"
      L"word = värde\n"
      L"mass = 42 ${unit}\n"
      L"n = 42\n");
  ini.parse(is);
  ini.default_section(ini.sections[L"DEFAULT"]);
  ini.interpolate();
  CHECK(ini.errors.empty());
  const inipp::Ini<wchar_t>::Section& w = ini.sections.at(L"w");
  CHECK(w.at(L"word") == L"värde");
  CHECK(w.at(L"mass") == L"42 kg");
  int n = 0;
  CHECK(inipp::get_value(w, L"n", n));
  CHECK(n == 42);
  std::wstring word;
  CHECK(inipp::get_value(w, L"word", word));
  CHECK(word == L"värde");

  std::wostringstream os;
  ini.generate(os);
  CHECK(os.str() ==
        L"[DEFAULT]\nunit=kg\n\n"
        L"[w]\nmass=42 kg\nn=42\nunit=kg\nword=värde\n\n");
}

}  // namespace

int main() {
  test_parse_and_extract();
  test_cyclic_interpolation_stops();
  test_custom_format();
  test_wide();
  if (failures != 0) {
    std::printf("%d failures\n", failures);
    return 1;
  }
  std::printf("inipp: ok\n");
  return 0;
}
