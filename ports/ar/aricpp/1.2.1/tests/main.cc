// Offline checks of aricpp's helpers, plus the error a Client request reports
// when it cannot resolve its server, which needs no socket.
#include <aricpp/basicauth.h>
#include <aricpp/bridge.h>
#include <aricpp/client.h>
#include <aricpp/jsontree.h>
#include <aricpp/method.h>
#include <aricpp/terminationdtmf.h>
#include <aricpp/urlencode.h>

#include <boost/asio.hpp>
#include <boost/beast/http.hpp>

#include <iostream>
#include <string>
#include <vector>

namespace asio = boost::asio;
namespace http = boost::beast::http;
using tcp = asio::ip::tcp;

static int failures = 0;
#define CHECK(cond)                                                      \
    do {                                                                 \
        if (!(cond)) {                                                   \
            std::cerr << "FAIL line " << __LINE__ << ": " #cond << '\n'; \
            ++failures;                                                  \
        }                                                                \
    } while (0)

int main() {
    CHECK(aricpp::UrlEncode("a b&c~") == "a%20b%26c~");
    CHECK(aricpp::UrlEncode("PJSIP/100") == "PJSIP%2F100");
    CHECK(aricpp::GetBasicAuth("user", "pass") == "Basic dXNlcjpwYXNz");
    CHECK(aricpp::GetBasicAuth("a", "b") == "Basic YTpi");
    CHECK(aricpp::GetBasicAuth(">>>", "???~") == "Basic Pj4+Oj8/P34=");
    CHECK(aricpp::ToString(aricpp::Method::get) == "GET");
    CHECK(aricpp::ToString(aricpp::Method::delete_) == "DELETE");
    CHECK(aricpp::ToBeast(aricpp::Method::post) == http::verb::post);
    CHECK(static_cast<std::string>(aricpp::TerminationDtmf::pound) == "#");
    CHECK(static_cast<std::string>(aricpp::TerminationDtmf::none) == "none");

    CHECK(static_cast<std::string>(aricpp::Bridge::Type::mixing) == "mixing");

    auto tree = aricpp::FromJson(R"({"channel":{"id":"123","name":"PJSIP/1"},"args":["a","b"]})");
    CHECK(aricpp::Get<std::string>(tree, {"channel", "id"}) == "123");
    CHECK(aricpp::Get<std::string>(tree, {"channel", "name"}) == "PJSIP/1");
    CHECK((aricpp::Get<std::vector<std::string>>(tree, {"args"}) == std::vector<std::string>{"a", "b"}));
    CHECK(aricpp::ToString(aricpp::FromJson(aricpp::ToString(tree))) == aricpp::ToString(tree));

    // An unknown service name fails the resolve step before any connection,
    // so the handler gets the error with no status.
    asio::io_context ios;
    aricpp::Client client(ios, "127.0.0.1", "no-such-service", "user", "pass", "app");
    bool answered = false;
    client.RawCmd(aricpp::Method::get, "/ari/channels",
                  [&](const boost::system::error_code& ec, unsigned status, const std::string&, const std::string& body) {
                      CHECK(!!ec);
                      CHECK(status == 0);
                      CHECK(body.empty());
                      answered = true;
                  });
    ios.run();
    CHECK(answered);
    return failures == 0 ? 0 : 1;
}
