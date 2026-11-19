// Connects to the MySQL server CI's Linux check starts (docs/maintaining.md,
// "CI") over TCP with TLS required, then asks the server which cipher the
// session uses and runs a statement whose parameters Boost.MySQL formats,
// checking the typed result.  A server that stops answering fails the probe
// after 60 seconds rather than hanging it.
#include <boost/asio/io_context.hpp>
#include <boost/mysql/any_connection.hpp>
#include <boost/mysql/connect_params.hpp>
#include <boost/mysql/error_with_diagnostics.hpp>
#include <boost/mysql/results.hpp>
#include <boost/mysql/ssl_mode.hpp>
#include <boost/mysql/with_params.hpp>

#include <unistd.h>

#include <cstdio>
#include <string>

namespace mysql = boost::mysql;

int main() {
  alarm(60);
  try {
    boost::asio::io_context io;
    mysql::any_connection connection(io);
    mysql::connect_params params;
    params.server_address.emplace_host_and_port("127.0.0.1");
    params.username = "root";
    params.password = "root";
    params.ssl = mysql::ssl_mode::require;
    connection.connect(params);

    mysql::results status;
    connection.execute("SHOW SESSION STATUS LIKE 'Ssl_cipher'", status);
    const std::string cipher =
        status.rows().size() == 1 ? std::string(status.rows().at(0).at(1).as_string()) : "";
    std::printf("character set %s, TLS cipher [%s]\n",
                std::string(connection.current_character_set()->name).c_str(), cipher.c_str());

    mysql::results product;
    connection.execute(mysql::with_params("SELECT {} * {}, CONCAT({}, {})", 6, 7, "Cabin", "'s"),
                       product);
    const auto row = product.rows().at(0);
    const bool ok = connection.uses_ssl() && !cipher.empty() && row.at(0).as_int64() == 42 &&
                    row.at(1).as_string() == "Cabin's";
    connection.close();
    if (!ok) {
      std::fputs("FAIL: no TLS session, or the wrong result\n", stderr);
      return 1;
    }
    std::puts("boost-mysql: a query over TLS returned the expected row");
    return 0;
  } catch (const mysql::error_with_diagnostics& error) {
    std::fprintf(stderr, "FAIL: %s: %s\n", error.what(),
                 std::string(error.get_diagnostics().server_message()).c_str());
    return 1;
  }
}
