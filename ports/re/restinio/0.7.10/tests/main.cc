#include <restinio/core.hpp>
#include <restinio/helpers/http_field_parsers/accept.hpp>
#include <restinio/helpers/http_field_parsers/content-type.hpp>
#include <restinio/utils/base64.hpp>
#include <restinio/utils/sha1.hpp>

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

namespace {

int failures = 0;

#define CHECK(cond)                                                       \
  do {                                                                    \
    if (!(cond)) {                                                        \
      std::cerr << __FILE__ << ':' << __LINE__ << ": " #cond << '\n';     \
      ++failures;                                                         \
    }                                                                     \
  } while (false)

// A request needs a connection; this one never touches a socket.
struct fake_connection_t : restinio::impl::connection_base_t {
  fake_connection_t() : restinio::impl::connection_base_t{0} {}
  void check_timeout(std::shared_ptr<restinio::tcp_connection_ctx_base_t>&) override {}
  void write_response_parts(restinio::request_id_t, restinio::response_output_flags_t,
                            restinio::write_group_t) override {}
};

restinio::request_handle_t make_request(std::string target,
                                        restinio::http_method_id_t method) {
  restinio::no_extra_data_factory_t factory;
  return std::make_shared<restinio::request_t>(
      0, restinio::http_request_header_t{method, std::move(target)}, "",
      std::make_shared<fake_connection_t>(),
      restinio::endpoint_t{restinio::asio_ns::ip::make_address_v4("127.0.0.1"), 3000},
      factory);
}

}  // namespace

int main() {
  // Express routes: named and indexed captures, regex-constrained, method.
  using router_t = restinio::router::express_router_t<>;
  router_t router;
  std::string hit, id, rest;
  std::uint32_t idx = 0;
  router.http_get(R"(/users/:id(\d+))", [&](auto, auto p) {
    hit = "user";
    id = restinio::cast_to<std::string>(p["id"]);
    idx = restinio::cast_to<std::uint32_t>(p["id"]);
    return restinio::request_accepted();
  });
  router.http_post("/files/:name", [&](auto, auto p) {
    hit = "post-file";
    id = restinio::cast_to<std::string>(p["name"]);
    return restinio::request_accepted();
  });
  router.http_get(R"(/img/(\w+)\.(png|gif))", [&](auto, auto p) {
    hit = "img";
    id = restinio::cast_to<std::string>(p[0]);
    rest = restinio::cast_to<std::string>(p[1]);
    return restinio::request_accepted();
  });

  CHECK(router(make_request("/users/42", restinio::http_method_get())) ==
        restinio::request_accepted());
  CHECK(hit == "user" && id == "42" && idx == 42);

  hit.clear();
  CHECK(router(make_request("/users/abc", restinio::http_method_get())) ==
        restinio::request_not_handled());
  CHECK(hit.empty());

  CHECK(router(make_request("/files/a.txt", restinio::http_method_get())) ==
        restinio::request_not_handled());
  CHECK(router(make_request("/files/a.txt", restinio::http_method_post())) ==
        restinio::request_accepted());
  CHECK(hit == "post-file" && id == "a.txt");

  CHECK(router(make_request("/img/logo.gif", restinio::http_method_get())) ==
        restinio::request_accepted());
  CHECK(hit == "img" && id == "logo" && rest == "gif");

  // Query string parsing.
  {
    auto q = restinio::parse_query("a=1&b=two%20words;c=%D0%9C");
    CHECK(q.size() == 3);
    CHECK(restinio::cast_to<int>(q["a"]) == 1);
    CHECK(q["b"] == "two words");
    CHECK(q["c"] == "\xD0\x9C");
    CHECK(!q.has("d"));
    CHECK(!restinio::try_parse_query<restinio::parse_query_traits::restinio_defaults>(restinio::string_view_t{"a=%zz"}));
  }

  // Percent encoding and base64/sha1 utilities.
  CHECK(restinio::utils::escape_percent_encoding("a b;c") == "a%20b%3Bc");
  CHECK(restinio::utils::unescape_percent_encoding("a%20b%3Bc") == "a b;c");
  CHECK(restinio::utils::base64::encode("Man") == "TWFu");
  CHECK(restinio::utils::base64::decode("TWFu") == "Man");
  CHECK(restinio::utils::sha1::to_hex_string(restinio::utils::sha1::make_digest(
            restinio::string_view_t{"abc"})) ==
        "a9993e364706816aba3e25717850c26c9cd0d89d");

  // HTTP field parsers.
  {
    using namespace restinio::http_field_parsers;
    auto ct = content_type_value_t::try_parse("Text/HTML; CharSet=utf-8");
    CHECK(ct);
    if (ct) {
      CHECK(ct->media_type.type == "text" && ct->media_type.subtype == "html");
      CHECK(ct->media_type.parameters.size() == 1);
      CHECK(ct->media_type.parameters.front().first == "charset");
      CHECK(ct->media_type.parameters.front().second == "utf-8");
    }
    auto acc = accept_value_t::try_parse("text/html, application/json;q=0.5");
    CHECK(acc);
    if (acc) {
      CHECK(acc->items.size() == 2);
      CHECK(acc->items[0].media_type.type == "text");
      CHECK(!acc->items[0].weight);
      CHECK(acc->items[1].media_type.subtype == "json");
      CHECK(acc->items[1].weight && acc->items[1].weight->as_uint() == 500);
    }
    CHECK(!content_type_value_t::try_parse("not a media type"));
  }

  return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
