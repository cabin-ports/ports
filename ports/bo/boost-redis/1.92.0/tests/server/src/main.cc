// Talks to the Redis server CI's Linux check installs (docs/maintaining.md,
// "CI"): one connection runs PING, SET, INCR, GET and DEL, and each reply
// comes back typed.  The exchange is cancelled after 30 seconds, so a
// missing server fails the probe rather than hanging it.
#include <boost/asio/cancel_after.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/redis/config.hpp>
#include <boost/redis/connection.hpp>
#include <boost/redis/request.hpp>
#include <boost/redis/response.hpp>
#include <boost/redis/src.hpp>

#include <chrono>
#include <cstdio>
#include <string>
#include <tuple>

namespace redis = boost::redis;

int main() {
  boost::asio::io_context io;
  redis::connection connection(io);

  redis::request req;
  req.push("PING", "cabin");
  req.push("SET", "cabin-probe", 41);
  req.push("INCR", "cabin-probe");
  req.push("GET", "cabin-probe");
  req.push("DEL", "cabin-probe");
  redis::response<std::string, std::string, long long, std::string, long long> resp;

  boost::system::error_code exec_error;
  bool finished = false;
  connection.async_run(redis::config{}, boost::asio::detached);
  connection.async_exec(req, resp,
                        boost::asio::cancel_after(std::chrono::seconds(30),
                                                  [&](boost::system::error_code ec, std::size_t) {
                                                    exec_error = ec;
                                                    finished = true;
                                                    connection.cancel();
                                                  }));
  io.run();

  if (!finished || exec_error) {
    std::fprintf(stderr, "FAIL: %s\n", exec_error.message().c_str());
    return 1;
  }
  const auto& [ping, set, incr, get, del] = resp;
  const bool ok = ping.has_value() && ping.value() == "cabin" && set.has_value() &&
                  set.value() == "OK" && incr.has_value() && incr.value() == 42 &&
                  get.has_value() && get.value() == "42" && del.has_value() && del.value() == 1;
  if (!ok) {
    std::fputs("FAIL: unexpected replies\n", stderr);
    return 1;
  }
  std::puts("boost-redis: PING, SET, INCR, GET and DEL answered as expected");
  return 0;
}
