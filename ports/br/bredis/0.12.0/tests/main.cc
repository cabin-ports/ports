#include <boost/asio.hpp>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "bredis/Connection.hpp"
#include "bredis/Extract.hpp"
#include "bredis/MarkerHelpers.hpp"
#include "bredis/Protocol.hpp"

namespace r = bredis;
namespace asio = boost::asio;

using Buffer = asio::const_buffer;
using Iterator = asio::buffers_iterator<Buffer, char>;
using Parsed = r::parse_result_mapper_t<Iterator, r::parsing_policy::keep_result>;

#define CHECK(cond)                                                       \
  do {                                                                    \
    if (!(cond)) {                                                        \
      std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, \
                   #cond);                                                \
      std::exit(1);                                                       \
    }                                                                     \
  } while (0)

static r::parse_result_t<Iterator, r::parsing_policy::keep_result> parse(
    const std::string &s) {
  Buffer b(s.data(), s.size());
  return r::Protocol::parse(Iterator::begin(b), Iterator::end(b));
}

static bool equals(const r::markers::redis_result_t<Iterator> &m,
                   const char *text) {
  return boost::apply_visitor(r::marker_helpers::equality<Iterator>(text), m);
}

static std::string contents(const asio::streambuf &buf) {
  return std::string(asio::buffers_begin(buf.data()),
                     asio::buffers_end(buf.data()));
}

static std::string serialized(const r::single_command_t &cmd) {
  asio::streambuf buf;
  r::Protocol::serialize(buf, cmd);
  return contents(buf);
}

// Hands out to_read, then later, a byte or two at a time.
struct MemoryStream {
  std::string written, to_read, later;

  template <typename Buffers>
  std::size_t write_some(const Buffers &b, boost::system::error_code &ec) {
    ec = {};
    std::size_t n = asio::buffer_size(b);
    std::string chunk(n, '\0');
    asio::buffer_copy(asio::buffer(&chunk[0], n), b);
    written += chunk;
    return n;
  }
  template <typename Buffers> std::size_t write_some(const Buffers &b) {
    boost::system::error_code ec;
    return write_some(b, ec);
  }

  template <typename Buffers>
  std::size_t read_some(const Buffers &b, boost::system::error_code &ec) {
    ec = {};
    if (to_read.empty()) {
      to_read.swap(later);
    }
    if (to_read.empty()) {
      ec = asio::error::eof;
      return 0;
    }
    std::size_t n = asio::buffer_copy(b, asio::buffer(to_read));
    to_read.erase(0, n);
    return n;
  }
  template <typename Buffers> std::size_t read_some(const Buffers &b) {
    boost::system::error_code ec;
    std::size_t n = read_some(b, ec);
    if (ec) throw boost::system::system_error(ec);
    return n;
  }
};

int main() {
  // Simple string, error, integer.
  {
    std::string s = "+OK\r\n";
    auto res = parse(s);
    auto *ok = boost::get<Parsed>(&res);
    CHECK(ok && ok->consumed == s.size());
    CHECK(boost::get<r::markers::string_t<Iterator>>(&ok->result));
    CHECK(equals(ok->result, "OK"));
  }
  {
    std::string s = "-ERR bad\r\n";
    auto res = parse(s);
    auto *ok = boost::get<Parsed>(&res);
    CHECK(ok && ok->consumed == s.size());
    CHECK(boost::get<r::markers::error_t<Iterator>>(&ok->result));
    CHECK(equals(ok->result, "ERR bad"));
  }
  {
    std::string s = ":-1234\r\n";
    auto res = parse(s);
    auto *ok = boost::get<Parsed>(&res);
    CHECK(ok && ok->consumed == s.size());
    CHECK(boost::get<r::markers::int_t<Iterator>>(&ok->result));
    CHECK(equals(ok->result, "-1234"));
  }

  // Bulk string and nil.
  {
    std::string s = "$5\r\nhello\r\n";
    auto res = parse(s);
    auto *ok = boost::get<Parsed>(&res);
    CHECK(ok && ok->consumed == s.size());
    CHECK(boost::get<r::markers::string_t<Iterator>>(&ok->result));
    CHECK(equals(ok->result, "hello"));
  }
  {
    std::string s = "$-1\r\n";
    auto res = parse(s);
    auto *ok = boost::get<Parsed>(&res);
    CHECK(ok && ok->consumed == s.size());
    CHECK(boost::get<r::markers::nil_t<Iterator>>(&ok->result));
  }

  // Nested array; only the first reply of the buffer is consumed.
  {
    std::string s = "*2\r\n*2\r\n:1\r\n$2\r\nab\r\n-E\r\n";
    const std::string input = s + "+extra\r\n";
    auto res = parse(input);
    auto *ok = boost::get<Parsed>(&res);
    CHECK(ok && ok->consumed == s.size());
    auto *outer = boost::get<r::markers::array_holder_t<Iterator>>(&ok->result);
    CHECK(outer && outer->elements.size() == 2);
    auto *inner =
        boost::get<r::markers::array_holder_t<Iterator>>(&outer->elements[0]);
    CHECK(inner && inner->elements.size() == 2);
    CHECK(boost::get<r::markers::int_t<Iterator>>(&inner->elements[0]));
    CHECK(equals(inner->elements[0], "1"));
    CHECK(equals(inner->elements[1], "ab"));
    CHECK(boost::get<r::markers::error_t<Iterator>>(&outer->elements[1]));
    CHECK(equals(outer->elements[1], "E"));

    // The extractor turns the markers into owning values.
    auto ex = boost::apply_visitor(r::extractor<Iterator>(), ok->result);
    auto *arr = boost::get<r::extracts::array_holder_t>(&ex);
    CHECK(arr && arr->elements.size() == 2);
    auto *in = boost::get<r::extracts::array_holder_t>(&arr->elements[0]);
    CHECK(in && in->elements.size() == 2);
    CHECK(boost::get<r::extracts::int_t>(in->elements[0]) == 1);
    CHECK(boost::get<r::extracts::string_t>(in->elements[1]).str == "ab");
    CHECK(boost::get<r::extracts::error_t>(arr->elements[1]).str == "E");
  }

  // Incomplete and malformed input.
  {
    const std::string s = "$5\r\nhel";
    auto res = parse(s);
    CHECK(boost::get<r::not_enough_data_t>(&res));
  }
  {
    const std::string s = "!OK\r\n";
    auto res = parse(s);
    auto *err = boost::get<r::protocol_error_t>(&res);
    CHECK(err && err->code.message() == "Wrong introduction");
  }

  // Serialization.
  CHECK(serialized(r::single_command_t("LLEN", "list")) ==
        "*2\r\n$4\r\nLLEN\r\n$4\r\nlist\r\n");
  CHECK(serialized(r::single_command_t("SET", "k", "")) ==
        "*3\r\n$3\r\nSET\r\n$1\r\nk\r\n$0\r\n\r\n");
  {
    r::command_wrapper_t cmds = r::command_container_t{
        r::single_command_t("PING"), r::single_command_t("GET", "k")};
    asio::streambuf buf;
    boost::apply_visitor(r::command_serializer_visitor<asio::streambuf>(buf),
                         cmds);
    CHECK(contents(buf) ==
          "*1\r\n$4\r\nPING\r\n*2\r\n$3\r\nGET\r\n$1\r\nk\r\n");
  }

  // Connection over an in-memory stream: the command reaches the stream as
  // RESP, and a reply delivered in two chunks is read back through the Asio
  // match condition.
  {
    r::Connection<MemoryStream> conn;
    conn.write(r::single_command_t("PING"));
    CHECK(conn.next_layer().written == "*1\r\n$4\r\nPING\r\n");

    conn.next_layer().to_read = "+PO";
    conn.next_layer().later = "NG\r\n";
    asio::streambuf rx;
    using RxIterator = r::to_iterator<asio::streambuf>::iterator_t;
    auto reply = conn.read(rx);
    CHECK(reply.consumed == 7);
    CHECK(boost::apply_visitor(r::marker_helpers::equality<RxIterator>("PONG"),
                               reply.result));
  }
  return 0;
}
