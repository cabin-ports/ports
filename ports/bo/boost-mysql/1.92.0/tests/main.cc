// Checks Boost.MySQL without a server: the SQL it composes and escapes, the
// encoding errors it reports, its date and field types, and a connection
// set up for TLS, which links OpenSSL.
#include <boost/asio/io_context.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/mysql/any_connection.hpp>
#include <boost/mysql/character_set.hpp>
#include <boost/mysql/client_errc.hpp>
#include <boost/mysql/date.hpp>
#include <boost/mysql/escape_string.hpp>
#include <boost/mysql/field.hpp>
#include <boost/mysql/format_sql.hpp>

#include <cstdint>
#include <cstdio>
#include <string>

namespace mysql = boost::mysql;

namespace {

bool ok = true;

void check(bool pass, const char* what) {
  if (!pass) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ok = false;
  }
}

void check_equal(const std::string& got, const std::string& want, const char* what) {
  if (got != want) {
    std::fprintf(stderr, "FAIL: %s: got [%s], want [%s]\n", what, got.c_str(), want.c_str());
    ok = false;
  }
}

}  // namespace

int main() {
  const mysql::format_options opts{mysql::utf8mb4_charset, true};

  check_equal(mysql::format_sql(opts, "SELECT * FROM {:i} WHERE id = {} AND name = {}",
                                "my`table", 42, "O'Brien"),
              "SELECT * FROM `my``table` WHERE id = 42 AND name = 'O\\'Brien'", "format_sql");

  std::string escaped;
  mysql::error_code ec =
      mysql::escape_string("a\"b\\c\n\x1a", opts, mysql::quoting_context::double_quote, escaped);
  check(!ec, "escape_string succeeds");
  check_equal(escaped, "a\\\"b\\\\c\\n\\Z", "escape_string");
  ec = mysql::escape_string("\xff", opts, mysql::quoting_context::single_quote, escaped);
  check(ec == mysql::client_errc::invalid_encoding, "escape_string refuses invalid UTF-8");

  check(mysql::date(2024, 2, 29).valid(), "2024-02-29 is a date");
  check(!mysql::date(2023, 2, 29).valid(), "2023-02-29 is not a date");

  const mysql::field value(std::int64_t{42});
  check(value.is_int64() && value.as_int64() == 42, "int64 field");
  check(mysql::field("text").as_string() == "text", "string field");

  boost::asio::io_context io;
  boost::asio::ssl::context tls(boost::asio::ssl::context::tls_client);
  check(tls.native_handle() != nullptr, "OpenSSL creates a TLS context");
  mysql::any_connection_params params;
  params.ssl_context = &tls;
  mysql::any_connection connection(io.get_executor(), params);
  check(!connection.uses_ssl(), "no TLS before connecting");

  if (!ok) return 1;
  std::puts("boost-mysql: checks passed");
  return 0;
}
