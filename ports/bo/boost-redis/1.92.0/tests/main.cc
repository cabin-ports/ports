// Checks Boost.Redis without a server: the RESP3 requests it writes, its
// RESP3 parser, and a connection given a TLS context, which links OpenSSL.
#include <boost/asio/io_context.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/redis/connection.hpp>
#include <boost/redis/request.hpp>
#include <boost/redis/resp3/node.hpp>
#include <boost/redis/resp3/parser.hpp>
#include <boost/redis/resp3/type.hpp>
#include <boost/redis/src.hpp>

#include <cstdio>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace redis = boost::redis;
using redis::resp3::type;

namespace {

bool ok = true;

void check(bool pass, const char* what) {
  if (!pass) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ok = false;
  }
}

bool is_node(const redis::resp3::basic_node<std::string>& node, type data_type,
             std::size_t aggregate_size, std::size_t depth, const char* value) {
  return node.data_type == data_type && node.aggregate_size == aggregate_size &&
         node.depth == depth && node.value == value;
}

}  // namespace

int main() {
  redis::request req;
  req.push("PING", "hello");
  req.push("SET", "key", 42);
  const std::vector<std::string> items{"a", "bc"};
  req.push_range("RPUSH", "list", items.begin(), items.end());
  check(req.get_commands() == 3, "three commands");
  check(req.payload() ==
            "*2\r\n$4\r\nPING\r\n$5\r\nhello\r\n"
            "*3\r\n$3\r\nSET\r\n$3\r\nkey\r\n$2\r\n42\r\n"
            "*4\r\n$5\r\nRPUSH\r\n$4\r\nlist\r\n$1\r\na\r\n$2\r\nbc\r\n",
        "RESP3 payload");

  const std::string_view reply = "*2\r\n+OK\r\n:42\r\n";
  redis::resp3::parser parser;
  boost::system::error_code ec;
  std::vector<redis::resp3::basic_node<std::string>> nodes;
  while (!parser.done()) {
    const auto node = parser.consume(reply, ec);
    if (ec || !node) break;
    nodes.push_back({node->data_type, node->aggregate_size, node->depth, std::string(node->value)});
  }
  check(!ec && parser.done() && nodes.size() == 3, "parses the reply");
  check(nodes.size() == 3 && is_node(nodes[0], type::array, 2, 0, "") &&
            is_node(nodes[1], type::simple_string, 1, 1, "OK") &&
            is_node(nodes[2], type::number, 1, 1, "42"),
        "reply nodes");

  boost::asio::io_context io;
  boost::asio::ssl::context tls(boost::asio::ssl::context::tlsv12_client);
  check(tls.native_handle() != nullptr, "OpenSSL creates a TLS context");
  redis::connection connection(io, std::move(tls));

  if (!ok) return 1;
  std::puts("boost-redis: checks passed");
  return 0;
}
