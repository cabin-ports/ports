#include <httplib.h>

#include <iostream>
#include <stdexcept>
#include <string>

static int failures = 0;

#define CHECK(cond)                                                            \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::cerr << __FILE__ << ":" << __LINE__ << ": " << #cond << "\n";       \
      ++failures;                                                              \
    }                                                                          \
  } while (0)

int main() {
  // Request header builders.
  auto basic = httplib::make_basic_authentication_header("user", "pass");
  CHECK(basic.first == "Authorization");
  CHECK(basic.second == "Basic dXNlcjpwYXNz");
  CHECK(httplib::make_basic_authentication_header("user", "pass", true).first ==
        "Proxy-Authorization");

  auto bearer = httplib::make_bearer_token_authentication_header("tok");
  CHECK(bearer.first == "Authorization");
  CHECK(bearer.second == "Bearer tok");

  CHECK(httplib::make_range_header({{0, 99}}).second == "bytes=0-99");
  CHECK(httplib::make_range_header({{0, 9}, {20, -1}, {-1, 5}}).second ==
        "bytes=0-9, 20-, -5");

  // URI and query coding.
  CHECK(httplib::encode_uri_component("a b/c?d=é") == "a%20b%2Fc%3Fd%3D%C3%A9");
  CHECK(httplib::decode_uri_component("a%20b%2Fc%3Fd%3D%C3%A9") == "a b/c?d=é");
  CHECK(httplib::encode_uri("http://x/a b?q=1") == "http://x/a%20b?q=1");
  CHECK(httplib::encode_query_component("a b&c") == "a+b%26c");
  CHECK(httplib::decode_query_component("a+b%26c") == "a b&c");
  CHECK(httplib::append_query_params("/p", {{"k", "v w"}, {"n", "1"}}) ==
        "/p?k=v+w&n=1");
  httplib::Params params;
  httplib::detail::parse_query_text(std::string("a=1&b=x%20y&a=2"), params);
  CHECK(params.size() == 3);
  CHECK(httplib::status_message(404) == std::string("Not Found"));

  // Response container, no connection involved.
  httplib::Response res;
  res.set_content("hello", "text/plain");
  res.set_header("X-Test", "42");
  CHECK(res.body == "hello");
  CHECK(res.get_header_value("Content-Type") == "text/plain");
  CHECK(res.get_header_value("x-test") == "42");
  CHECK(res.has_header("X-Test") && !res.has_header("X-Other"));
  CHECK(res.get_header_value("X-Other", "dflt") == "dflt");
  res.set_redirect("/next");
  CHECK(res.status == 302 && res.get_header_value("Location") == "/next");

  // Client construction parses the URL and never connects.
  httplib::Client cli("http://localhost:8080");
  CHECK(cli.is_valid() && cli.host() == "localhost" && cli.port() == 8080);
  httplib::Client dflt("http://example.com");
  CHECK(dflt.host() == "example.com" && dflt.port() == 80);
  httplib::Client hp("example.org", 9000);
  CHECK(hp.host() == "example.org" && hp.port() == 9000);
  bool threw = false;
  try {
    httplib::Client bad("ftp://example.com");
  } catch (const std::invalid_argument &) {
    threw = true;
  }
  CHECK(threw);

  // Server route registration chains and is not listening.
  httplib::Server svr;
  auto &same = svr.Get("/hi", [](const httplib::Request &,
                                 httplib::Response &r) {
    r.set_content("x", "text/plain");
  });
  CHECK(&same == &svr);
  CHECK(&svr.Post("/p", [](const httplib::Request &, httplib::Response &) {}) ==
        &svr);
  CHECK(!svr.is_running() && svr.is_valid());
  svr.CustomRoute("BAD METHOD", "/x",
                  [](const httplib::Request &, httplib::Response &) {});
  CHECK(!svr.is_valid());

  return failures == 0 ? 0 : 1;
}
