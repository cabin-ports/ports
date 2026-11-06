// Consumer test: a grammar with actions over memory and file inputs,
// local failure versus a must<> error with its position, a parse tree,
// grammar analysis, and the JSON, URI and UTF-8 rules PEGTL ships.
#include <tao/pegtl.hpp>
#include <tao/pegtl/contrib/analyze.hpp>
#include <tao/pegtl/contrib/json.hpp>
#include <tao/pegtl/contrib/parse_tree.hpp>
#include <tao/pegtl/contrib/uri.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <string_view>
#include <system_error>

namespace pegtl = tao::pegtl;

namespace {

int failures = 0;

void check(bool ok, const char* what, int line) {
  if (!ok) {
    std::fprintf(stderr, "main.cc:%d: check failed: %s\n", line, what);
    ++failures;
  }
}
#define CHECK(...) check((__VA_ARGS__), #__VA_ARGS__, __LINE__)

// A comma-separated list of integers, with optional blanks around them.
struct integer : pegtl::plus<pegtl::digit> {};
struct item : pegtl::pad<integer, pegtl::blank> {};
struct sum_grammar : pegtl::seq<pegtl::list<item, pegtl::one<','>>, pegtl::eof> {};

template <typename Rule>
struct sum_action : pegtl::nothing<Rule> {};

template <>
struct sum_action<integer> {
  template <typename ActionInput>
  static void apply(const ActionInput& in, long& total) {
    total += std::stol(in.string());
  }
};

// A bracketed digit: anything else after '[' is a hard error.
struct bracketed : pegtl::seq<pegtl::one<'['>, pegtl::must<pegtl::digit>, pegtl::one<']'>> {};

// Products of integers, for the parse tree.
struct times : pegtl::pad<pegtl::one<'*'>, pegtl::blank> {};
struct product : pegtl::seq<pegtl::list<integer, times>, pegtl::eof> {};

template <typename Rule>
using product_selector = pegtl::parse_tree::selector<
    Rule, pegtl::parse_tree::store_content::on<integer>>;

template <typename Rule>
bool matches(std::string_view text) {
  pegtl::memory_input in(text.data(), text.size(), "text");
  return pegtl::parse<pegtl::seq<Rule, pegtl::eof>>(in);
}

}  // namespace

int main() {
  CHECK(std::string_view(TAO_PEGTL_VERSION) == "3.2.8");

  // Actions see each matched integer; a list that does not match fails
  // locally and parse() returns false.
  long total = 0;
  pegtl::memory_input sums(" 1, 2,3 , 40", "sums");
  CHECK(pegtl::parse<sum_grammar, sum_action>(sums, total) && total == 46);
  total = 0;
  pegtl::memory_input broken("1,,2", "broken");
  CHECK(!pegtl::parse<sum_grammar, sum_action>(broken, total));

  // must<> turns the failure into a parse_error at the failing position.
  bool raised = false;
  try {
    pegtl::memory_input in("[x]", "bracket");
    pegtl::parse<bracketed>(in);
  } catch (const pegtl::parse_error& e) {
    raised = true;
    CHECK(e.positions().size() == 1);
    const pegtl::position& at = e.positions().front();
    CHECK(at.byte == 1 && at.line == 1 && at.column == 2);
    CHECK(at.source == "bracket");
  }
  CHECK(raised);

  // The parse tree keeps one node per selected integer, with its text.
  pegtl::memory_input factors("2 * 3*44", "factors");
  const auto root = pegtl::parse_tree::parse<product, product_selector>(factors);
  CHECK(root != nullptr && root->is_root() && root->children.size() == 3);
  if (root && root->children.size() == 3) {
    CHECK(root->children[0]->is_type<integer>());
    CHECK(root->children[0]->string() == "2");
    CHECK(root->children[2]->string() == "44");
    CHECK(root->children[2]->begin().column == 7);
  }

  // The grammars have no left recursion or other issues analyze() finds.
  CHECK(pegtl::analyze<sum_grammar>(0) == 0);
  CHECK(pegtl::analyze<pegtl::json::text>(0) == 0);

  // The bundled JSON, URI and UTF-8 grammars.
  CHECK(matches<pegtl::json::text>(R"({"a": [1, 2.5e3, true, null, "é"]})"));
  CHECK(!matches<pegtl::json::text>(R"({"a": })"));
  CHECK(matches<pegtl::uri::URI>("https://user@example.com:8080/p/a?q=1#frag"));
  CHECK(!matches<pegtl::uri::URI>("no scheme"));
  CHECK(matches<pegtl::plus<pegtl::utf8::any>>("caf\xc3\xa9"));
  CHECK(!matches<pegtl::plus<pegtl::utf8::any>>("caf\xc3"));

  // file_input reads a file through std::filesystem, from a directory of
  // this run's own: create_directory() returns false if the name is taken.
  std::random_device entropy;
  std::filesystem::path dir;
  do {
    dir = std::filesystem::temp_directory_path() /
          ("cabin-pegtl-test-" + std::to_string(entropy()));
  } while (!std::filesystem::create_directory(dir));
  const std::filesystem::path path = dir / "sums.txt";
  {
    std::ofstream out(path, std::ios::binary);
    out << "10,20,30";
    out.close();
    CHECK(!out.fail());
  }
  {
    total = 0;
    pegtl::file_input in(path);
    CHECK(pegtl::parse<sum_grammar, sum_action>(in, total) && total == 60);
  }
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);

  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("ok");
  return 0;
}
