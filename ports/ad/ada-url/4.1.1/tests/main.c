#include <ada_c.h>

#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(cond)                                         \
  do {                                                      \
    if (!(cond)) {                                          \
      fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #cond); \
      ++failures;                                           \
    }                                                       \
  } while (0)

static int eq(ada_string s, const char* expected) {
  return s.length == strlen(expected) && memcmp(s.data, expected, s.length) == 0;
}

#define LIT(s) s, strlen(s)

int main(void) {
  const char* in =
      "https://username:password@www.google.com:8080/pathname?query=true#hash-exists";
  ada_url u = ada_parse(LIT(in));
  CHECK(ada_is_valid(u));
  CHECK(eq(ada_get_href(u), in));
  CHECK(eq(ada_get_protocol(u), "https:"));
  CHECK(eq(ada_get_username(u), "username"));
  CHECK(eq(ada_get_password(u), "password"));
  CHECK(eq(ada_get_host(u), "www.google.com:8080"));
  CHECK(eq(ada_get_hostname(u), "www.google.com"));
  CHECK(eq(ada_get_port(u), "8080"));
  CHECK(eq(ada_get_pathname(u), "/pathname"));
  CHECK(eq(ada_get_search(u), "?query=true"));
  CHECK(eq(ada_get_hash(u), "#hash-exists"));
  CHECK(ada_has_credentials(u) && ada_has_port(u) && ada_has_search(u));

  ada_owned_string origin = ada_get_origin(u);
  CHECK(origin.length == strlen("https://www.google.com:8080") &&
        memcmp(origin.data, "https://www.google.com:8080", origin.length) == 0);
  ada_free_owned_string(origin);

  ada_set_hash(u, LIT("new-hash"));
  CHECK(ada_set_host(u, LIT("changed-host:9090")));
  CHECK(ada_set_pathname(u, LIT("new-pathname")));
  ada_set_search(u, LIT("new-search"));
  CHECK(ada_set_protocol(u, LIT("wss")));
  CHECK(eq(ada_get_href(u),
           "wss://username:password@changed-host:9090/new-pathname?new-search#new-hash"));
  ada_free(u);

  /* Normalization: IPv4 forms, default port, dot segments, percent-encoding. */
  u = ada_parse(LIT("http://0x7f.1/"));
  CHECK(ada_is_valid(u) && eq(ada_get_href(u), "http://127.0.0.1/"));
  ada_free(u);
  u = ada_parse(LIT("https://example.com:443/a b/../c"));
  CHECK(ada_is_valid(u) && eq(ada_get_href(u), "https://example.com/c"));
  CHECK(!ada_has_port(u));
  ada_free(u);
  u = ada_parse(LIT("https://example.com/a b"));
  CHECK(ada_is_valid(u) && eq(ada_get_pathname(u), "/a%20b"));
  ada_free(u);

  u = ada_parse_with_base(LIT("../a?x#y"), LIT("https://example.org/b/c/d"));
  CHECK(ada_is_valid(u) && eq(ada_get_href(u), "https://example.org/b/a?x#y"));
  ada_free(u);

  CHECK(!ada_can_parse(LIT("/relative")));
  CHECK(ada_can_parse_with_base(LIT("/x"), LIT("https://a.org")));
  u = ada_parse(LIT("http://exa mple.com"));
  CHECK(!ada_is_valid(u));
  ada_free(u);

  /* IDNA (the bundled ada-idna) in both directions. */
  ada_owned_string a = ada_idna_to_ascii(LIT("b\xc3\xbc" "cher.de"));
  CHECK(a.length == strlen("xn--bcher-kva.de") &&
        memcmp(a.data, "xn--bcher-kva.de", a.length) == 0);
  ada_free_owned_string(a);
  a = ada_idna_to_unicode(LIT("xn--bcher-kva.de"));
  CHECK(a.length == strlen("b\xc3\xbc" "cher.de") &&
        memcmp(a.data, "b\xc3\xbc" "cher.de", a.length) == 0);
  ada_free_owned_string(a);

  /* Search params. */
  ada_url_search_params p = ada_parse_search_params(LIT("a=1&b=2&a=3"));
  CHECK(ada_search_params_size(p) == 3);
  CHECK(ada_search_params_has(p, LIT("b")) && !ada_search_params_has(p, LIT("c")));
  CHECK(eq(ada_search_params_get(p, LIT("a")), "1"));
  ada_strings all = ada_search_params_get_all(p, LIT("a"));
  CHECK(ada_strings_size(all) == 2 && eq(ada_strings_get(all, 0), "1") &&
        eq(ada_strings_get(all, 1), "3"));
  ada_free_strings(all);
  ada_search_params_sort(p);
  ada_search_params_append(p, LIT("c"), LIT("x y"));
  a = ada_search_params_to_string(p);
  CHECK(a.length == strlen("a=1&a=3&b=2&c=x+y") &&
        memcmp(a.data, "a=1&a=3&b=2&c=x+y", a.length) == 0);
  ada_free_owned_string(a);
  ada_free_search_params(p);

  return failures ? 1 : 0;
}
