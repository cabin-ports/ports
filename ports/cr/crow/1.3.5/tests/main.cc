// Offline checks: the sandbox and CI probe allow no sockets, so routing goes
// through app.handle_full as upstream's tests/unittest.cpp PathRouting does.
#include <crow.h>

#include <cstdio>
#include <string>

namespace {

int failures = 0;

void check(bool ok, const char *what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

} // namespace

int main() {
  // JSON: build, serialize, parse, read back.
  crow::json::wvalue w;
  w["name"] = "crow";
  w["n"] = 42;
  w["list"][0] = 1;
  w["list"][1] = 2;
  // Objects iterate in the standard library's hash order, so compare parsed.
  check(w["list"].dump() == "[1,2]", "json list dump");
  auto back = crow::json::load(w.dump());
  check(back["name"].s() == "crow" && back["n"].i() == 42 &&
            back["list"].size() == 2 && back["list"][1].i() == 2,
        "json round trip");
  auto r = crow::json::load(R"({"a":{"b":[1,2,3]},"s":"xé","t":true})");
  check(static_cast<bool>(r), "json load");
  check(r["a"]["b"].size() == 3 && r["a"]["b"][2].i() == 3, "json array");
  check(r["s"].s() == "x\xc3\xa9", "json unicode escape");
  check(r["t"].b(), "json bool");
  check(!crow::json::load("{\"a\":"), "json rejects truncated input");

  // qs_parse: percent decoding and repeated keys.
  crow::query_string qs("a=1&b=hello%20world&c[]=x&c[]=y", false);
  check(std::string(qs.get("a")) == "1", "query a");
  check(std::string(qs.get("b")) == "hello world", "query percent decoding");
  check(qs.get_list("c").size() == 2, "query list");
  check(qs.get("missing") == nullptr, "query missing key");

  // base64.
  check(crow::utility::base64encode("Man", 3) == "TWFu", "base64 encode");
  check(crow::utility::base64encode("Ma", 2) == "TWE=", "base64 padding");
  check(crow::utility::base64decode("TWFueQ==") == "Many", "base64 decode");

  // TinySHA1: FIPS 180 test vector for "abc".
  sha1::SHA1 sha;
  sha.processBytes("abc", 3);
  sha1::SHA1::digest32_t d;
  sha.getDigest(d);
  check(d[0] == 0xa9993e36 && d[1] == 0x4706816a && d[2] == 0xba3e2571 &&
            d[3] == 0x7850c26c && d[4] == 0x9cd0d89d,
        "sha1 abc");

  // Mustache.
  crow::mustache::context ctx;
  ctx["name"] = "Crow";
  ctx["items"][0]["v"] = "a";
  ctx["items"][1]["v"] = "b";
  check(crow::mustache::compile("Hi {{name}}:{{#items}}[{{v}}]{{/items}}")
                .render_string(ctx) == "Hi Crow:[a][b]",
        "mustache sections");

  // Routing.
  crow::SimpleApp app;
  CROW_ROUTE(app, "/add/<int>/<int>")([](int a, int b) {
    return std::to_string(a + b);
  });
  CROW_ROUTE(app, "/q")([](const crow::request &req) {
    return std::string(req.url_params.get("k"));
  });
  app.validate();
  {
    crow::request req;
    crow::response res;
    req.url = "/add/2/40";
    app.handle_full(req, res);
    check(res.code == 200 && res.body == "42", "route params");
  }
  {
    crow::request req;
    crow::response res;
    req.url = "/q";
    req.url_params = crow::query_string("k=v", false);
    app.handle_full(req, res);
    check(res.code == 200 && res.body == "v", "route query string");
  }
  {
    crow::request req;
    crow::response res;
    req.url = "/nowhere";
    app.handle_full(req, res);
    check(res.code == 404, "route 404");
  }

  // Instantiate the asio-based server without opening a socket.
  auto run = &crow::SimpleApp::run;
  (void)run;

  return failures == 0 ? 0 : 1;
}
