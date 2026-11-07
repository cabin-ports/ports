// Consumer test (C++98): in-situ parsing with entity expansion, the
// parser flags (declaration and comment nodes, whitespace trimming and
// normalization, closing-tag validation, non-destructive mode), parse
// errors with their positions, DOM construction and editing, printing
// with and without indentation (rapidxml_print.hpp, as patched), a
// user-supplied allocator, wide characters, and rapidxml_utils.hpp's
// stream loader and counters.
#include <rapidxml.hpp>
#include <rapidxml_print.hpp>
#include <rapidxml_utils.hpp>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char* what, int line) {
  if (!ok) {
    std::fprintf(stderr, "main.cc:%d: check failed: %s\n", line, what);
    ++failures;
  }
}
#define CHECK(cond) check((cond), #cond, __LINE__)

std::size_t allocations = 0;
std::size_t frees = 0;

void* counting_alloc(std::size_t size) {
  ++allocations;
  return std::malloc(size);
}

void counting_free(void* p) {
  ++frees;
  std::free(p);
}

std::string print_flat(const rapidxml::xml_node<>& node) {
  std::string out;
  rapidxml::print(std::back_inserter(out), node, rapidxml::print_no_indenting);
  return out;
}

}  // namespace

int main() {
  using namespace rapidxml;

  // In-situ parse with the default flags: entities and character
  // references expand, the declaration and comments are skipped, and an
  // element's value is its first data node.
  char text[] =
      "<?xml version=\"1.0\"?><!-- c --><catalog lang=\"en\">"
      "<book id=\"b1\" title=\"A &amp; B\">Tom &lt;3 &#x41;&#66;</book>"
      "<book id=\"b2\"/><![CDATA[<raw>]]></catalog>";
  xml_document<> doc;
  doc.parse<0>(text);
  xml_node<>* catalog = doc.first_node("catalog");
  CHECK(catalog != 0);
  if (catalog != 0) {
    CHECK(doc.first_node() == catalog);
    CHECK(catalog->parent() == &doc);
    CHECK(std::string(catalog->first_attribute("lang")->value()) == "en");
    CHECK(count_children(catalog) == 3);

    xml_node<>* book1 = catalog->first_node("book");
    CHECK(count_attributes(book1) == 2);
    CHECK(std::string(book1->first_attribute("id")->value()) == "b1");
    CHECK(std::string(book1->first_attribute("title")->value()) == "A & B");
    CHECK(std::string(book1->value()) == "Tom <3 AB");
    CHECK(book1->first_node()->type() == node_data);
    CHECK(book1->parent() == catalog);

    xml_node<>* book2 = book1->next_sibling("book");
    CHECK(std::string(book2->first_attribute("id")->value()) == "b2");
    CHECK(book2->first_node() == 0);
    CHECK(book2->value_size() == 0);

    xml_node<>* cdata = book2->next_sibling();
    CHECK(cdata->type() == node_cdata);
    CHECK(std::string(cdata->value()) == "<raw>");
    CHECK(cdata->next_sibling() == 0);
    CHECK(catalog->last_node() == cdata);

    CHECK(print_flat(doc) ==
          "<catalog lang=\"en\"><book id=\"b1\" title=\"A &amp; B\">Tom &lt;3 AB</book>"
          "<book id=\"b2\"/><![CDATA[<raw>]]></catalog>");
  }

  // Declaration and comment nodes only when asked for.
  char with_decl[] = "<?xml version=\"1.0\"?><!-- note --><r/>";
  doc.parse<parse_declaration_node | parse_comment_nodes>(with_decl);
  xml_node<>* decl = doc.first_node();
  CHECK(decl->type() == node_declaration);
  CHECK(std::string(decl->first_attribute("version")->value()) == "1.0");
  CHECK(decl->next_sibling()->type() == node_comment);
  CHECK(std::string(decl->next_sibling()->value()) == " note ");
  CHECK(std::string(decl->next_sibling()->next_sibling()->name()) == "r");

  // Whitespace stays as written unless trimmed and normalized.
  char spaced1[] = "<a>  x   y  </a>";
  doc.parse<0>(spaced1);
  CHECK(std::string(doc.first_node()->value()) == "  x   y  ");
  char spaced2[] = "<a>  x   y  </a>";
  doc.parse<parse_trim_whitespace | parse_normalize_whitespace>(spaced2);
  CHECK(std::string(doc.first_node()->value()) == "x y");

  // Errors carry a message and the position in the source text.
  char unclosed[] = "<a>";
  std::string message;
  long where = -1;
  try {
    doc.parse<0>(unclosed);
  } catch (const parse_error& e) {
    message = e.what();
    where = static_cast<long>(e.where<char>() - unclosed);
  }
  CHECK(message == "unexpected end of data");
  CHECK(where == 3);

  // Closing tags are checked only with parse_validate_closing_tags.
  char mismatched1[] = "<a></b>";
  doc.parse<0>(mismatched1);
  CHECK(std::string(doc.first_node()->name()) == "a");
  char mismatched2[] = "<a></b>";
  message.clear();
  where = -1;
  try {
    doc.parse<parse_validate_closing_tags>(mismatched2);
  } catch (const parse_error& e) {
    message = e.what();
    where = static_cast<long>(e.where<char>() - mismatched2);
  }
  CHECK(message == "invalid closing tag name");
  CHECK(where == 6);

  // Non-destructive mode leaves the source untouched: names and values
  // point into it, unterminated and unexpanded.
  const char original[] = "<a k=\"1 &amp; 2\">x &lt; y</a>";
  std::vector<char> source(original, original + sizeof original);
  doc.parse<parse_non_destructive>(&source[0]);
  CHECK(std::memcmp(&source[0], original, sizeof original) == 0);
  xml_node<>* a = doc.first_node("a");
  CHECK(a != 0);
  if (a != 0) {
    CHECK(std::string(a->name(), a->name_size()) == "a");
    xml_attribute<>* k = a->first_attribute("k");
    CHECK(std::string(k->value(), k->value_size()) == "1 &amp; 2");
    CHECK(std::string(a->value(), a->value_size()) == "x &lt; y");
  }

  // Build a document; printing escapes text and picks attribute quotes.
  xml_document<> out;
  xml_node<>* root = out.allocate_node(node_element, "root");
  out.append_node(root);
  xml_node<>* item = out.allocate_node(node_element, "item", "a<b & \"c\"");
  item->append_attribute(out.allocate_attribute("id", "1"));
  item->append_attribute(out.allocate_attribute("q", "say \"hi\""));
  root->append_node(item);
  xml_node<>* empty = out.allocate_node(node_element, "empty");
  root->append_node(empty);
  char* owned = out.allocate_string("copied");
  CHECK(std::strcmp(owned, "copied") == 0);
  CHECK(print_flat(out) ==
        "<root><item id=\"1\" q='say \"hi\"'>a&lt;b &amp; &quot;c&quot;</item>"
        "<empty/></root>");

  std::string indented;
  print(std::back_inserter(indented), *root);
  const std::string expected_indented =
      "<root>\n\t<item id=\"1\" q='say \"hi\"'>a&lt;b &amp; &quot;c&quot;</item>\n"
      "\t<empty/>\n</root>\n";
  CHECK(indented == expected_indented);
  std::ostringstream stream;
  stream << *root;
  CHECK(stream.str() == expected_indented);

  xml_node<>* copy = out.clone_node(root);
  CHECK(print_flat(*copy) == print_flat(*root));

  root->remove_node(empty);
  item->remove_attribute(item->first_attribute("q"));
  CHECK(count_children(root) == 1);
  CHECK(count_attributes(item) == 1);
  CHECK(print_flat(*root) ==
        "<root><item id=\"1\">a&lt;b &amp; &quot;c&quot;</item></root>");

  // A user allocator serves the pool once its static block is used up,
  // and gets every block back on clear().
  {
    xml_document<> pooled;
    pooled.set_allocator(counting_alloc, counting_free);
    pooled.append_node(pooled.allocate_node(node_element, "n"));
    CHECK(allocations == 0);
    for (int i = 1; i < 5000; ++i)
      pooled.append_node(pooled.allocate_node(node_element, "n"));
    CHECK(count_children(&pooled) == 5000);
    CHECK(allocations > 0);
    pooled.clear();
    CHECK(frees == allocations);
  }

  // Wide characters.
  wchar_t wide[] = L"<w a=\"b\">v</w>";
  xml_document<wchar_t> wdoc;
  wdoc.parse<0>(wide);
  CHECK(std::wstring(wdoc.first_node(L"w")->first_attribute(L"a")->value()) == L"b");
  CHECK(std::wstring(wdoc.first_node()->value()) == L"v");

  // rapidxml_utils.hpp: load a zero-terminated copy of a stream.
  std::istringstream in("<r><c/></r>");
  file<> loaded(in);
  CHECK(loaded.size() == 12);
  CHECK(loaded.data()[11] == '\0');
  doc.parse<0>(loaded.data());
  CHECK(std::string(doc.first_node("r")->first_node()->name()) == "c");

  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("rapidxml consumer test passed");
  return 0;
}
