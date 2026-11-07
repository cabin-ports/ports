// gumbo.h from C++ (its declarations are extern "C"): a document taken from
// the middle of a larger buffer by explicit length, walked recursively to
// collect its links and visible text into std::vector<std::string>.
#include <gumbo.h>

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, int line, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "main.cc:%d: check failed: %s\n", line, what);
    ++failures;
  }
}

#define CHECK(expr) check((expr), __LINE__, #expr)

struct Walk {
  std::vector<std::string> links;
  std::string text;
};

void walk(const GumboNode* node, Walk& w) {
  if (node->type == GUMBO_NODE_TEXT) {
    w.text += node->v.text.text;
    return;
  }
  if (node->type != GUMBO_NODE_ELEMENT) return;
  const GumboElement& e = node->v.element;
  if (e.tag == GUMBO_TAG_SCRIPT) return;
  if (e.tag == GUMBO_TAG_A) {
    if (const GumboAttribute* href = gumbo_get_attribute(&e.attributes, "href"))
      w.links.push_back(href->value);
  }
  for (unsigned int i = 0; i < e.children.length; ++i)
    walk(static_cast<const GumboNode*>(e.children.data[i]), w);
}

}  // namespace

int main() {
  // Only the part between the markers is parsed; it is not NUL-terminated.
  const std::string buffer =
      "IGNORED<ul><li><a href='/one'>One</a><li><a HREF=\"/two?x=1&amp;y=2\">"
      "Two</a><script>var a = '<a href=no>';</script></ul>IGNORED";
  const std::size_t begin = buffer.find("<ul>");
  const std::size_t end = buffer.rfind("IGNORED");

  std::unique_ptr<GumboOutput, void (*)(GumboOutput*)> out(
      gumbo_parse_with_options(&kGumboDefaultOptions, buffer.data() + begin,
                               end - begin),
      [](GumboOutput* o) { gumbo_destroy_output(&kGumboDefaultOptions, o); });

  Walk w;
  walk(out->root, w);
  CHECK(w.links.size() == 2);
  if (w.links.size() == 2) {
    CHECK(w.links[0] == "/one");
    CHECK(w.links[1] == "/two?x=1&y=2");
  }
  CHECK(w.text == "OneTwo");

  if (failures != 0) return 1;
  std::puts("gumbo 0.14.1: C++ API OK");
  return 0;
}
