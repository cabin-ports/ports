#include <ada.h>

#include <cstdio>
#include <string>
#include <vector>

static int failures;

#define CHECK(cond)                                              \
  do {                                                           \
    if (!(cond)) {                                               \
      std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #cond); \
      ++failures;                                                \
    }                                                            \
  } while (0)

int main() {
  auto url = ada::parse<ada::url_aggregator>("https://www.google.com");
  CHECK(url);
  CHECK(url->set_pathname("/my-super-long-path"));
  CHECK(url->get_href() == "https://www.google.com/my-super-long-path");
  url->set_search("a=b c");
  CHECK(url->get_search() == "?a=b%20c");
  CHECK(url->get_origin() == "https://www.google.com");

  auto full = ada::parse<ada::url>("https://user:pw@EXAMPLE.com:8080/a/./b/../c?q#f");
  CHECK(full);
  CHECK(full->get_href() == "https://user:pw@example.com:8080/a/c?q#f");
  CHECK(full->get_hostname() == "example.com");
  CHECK(full->get_port() == "8080");

  auto idn = ada::parse<ada::url>("https://B\xc3\x9c" "CHER.de");
  CHECK(idn);
  CHECK(idn->get_hostname() == "xn--bcher-kva.de");

  CHECK(!ada::parse<ada::url_aggregator>("http://exa mple.com"));
  auto base = ada::parse<ada::url_aggregator>("https://a.org/b/c/d");
  CHECK(base);
  auto rel = ada::parse<ada::url_aggregator>("../x", &*base);
  CHECK(rel && rel->get_href() == "https://a.org/b/x");

  CHECK(ada::idna::to_ascii("me\xc3\x9f" "agefactory.ca") == "xn--meagefactory-m9a.ca");
  CHECK(ada::idna::to_unicode("xn--bcher-kva.de") == "b\xc3\xbc" "cher.de");

  ada::url_search_params p("a=1&b=2&a=3");
  CHECK((p.get_all("a") == std::vector<std::string>{"1", "3"}));
  p.sort();
  CHECK(p.to_string() == "a=1&a=3&b=2");

  return failures ? 1 : 0;
}
