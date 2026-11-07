// http_parser.h from C++ (its declarations are extern "C"): two pipelined
// responses, fed in two pieces split inside a header name, collected into
// std::string and std::map through captureless lambdas, and a URL split into
// its parts.
#include <http_parser.h>

#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, int line, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "main.cc:%d: check failed: %s\n", line, what);
    ++failures;
  }
}

#define CHECK(expr) check((expr), __LINE__, #expr)

struct Message {
  unsigned status = 0;
  std::string reason, body;
  std::map<std::string, std::string> headers;
  bool keep_alive = false;
  bool complete = false;
};

struct Connection {
  std::vector<Message> messages;
  std::string field;
  bool in_value = false;
};

Connection& conn(http_parser* p) { return *static_cast<Connection*>(p->data); }

std::string part(const std::string& url, const http_parser_url& u,
                 http_parser_url_fields f) {
  if (!(u.field_set & (1 << f))) return "<unset>";
  return url.substr(u.field_data[f].off, u.field_data[f].len);
}

}  // namespace

int main() {
  http_parser_settings settings;
  http_parser_settings_init(&settings);
  settings.on_message_begin = [](http_parser* p) {
    conn(p).messages.emplace_back();
    conn(p).field.clear();
    conn(p).in_value = false;
    return 0;
  };
  settings.on_status = [](http_parser* p, const char* at, std::size_t len) {
    conn(p).messages.back().reason.append(at, len);
    return 0;
  };
  settings.on_header_field = [](http_parser* p, const char* at,
                                std::size_t len) {
    Connection& c = conn(p);
    if (c.in_value) {
      c.field.clear();
      c.in_value = false;
    }
    c.field.append(at, len);
    return 0;
  };
  settings.on_header_value = [](http_parser* p, const char* at,
                                std::size_t len) {
    Connection& c = conn(p);
    c.in_value = true;
    c.messages.back().headers[c.field].append(at, len);
    return 0;
  };
  settings.on_headers_complete = [](http_parser* p) {
    Message& m = conn(p).messages.back();
    m.status = p->status_code;
    m.keep_alive = http_should_keep_alive(p) != 0;
    return 0;
  };
  settings.on_body = [](http_parser* p, const char* at, std::size_t len) {
    conn(p).messages.back().body.append(at, len);
    return 0;
  };
  settings.on_message_complete = [](http_parser* p) {
    conn(p).messages.back().complete = true;
    return 0;
  };

  const std::string stream =
      "HTTP/1.1 200 OK\r\n"
      "Content-Type: text/plain\r\n"
      "Content-Length: 5\r\n"
      "\r\n"
      "first"
      "HTTP/1.1 301 Moved Permanently\r\n"
      "Location: /new\r\n"
      "Connection: close\r\n"
      "Transfer-Encoding: chunked\r\n"
      "\r\n"
      "3\r\nsec\r\n3\r\nond\r\n0\r\n\r\n";
  const std::size_t split = stream.find("Location") + 3;

  Connection c;
  http_parser parser;
  parser.data = &c;  // http_parser_init keeps the data pointer it finds.
  http_parser_init(&parser, HTTP_RESPONSE);
  std::size_t n = http_parser_execute(&parser, &settings, stream.data(), split);
  n += http_parser_execute(&parser, &settings, stream.data() + split,
                           stream.size() - split);
  CHECK(n == stream.size());
  CHECK(std::string(http_errno_name(HTTP_PARSER_ERRNO(&parser))) == "HPE_OK");
  CHECK(c.messages.size() == 2);
  if (c.messages.size() == 2) {
    const Message& a = c.messages[0];
    CHECK(a.complete && a.status == 200 && a.reason == "OK");
    CHECK(a.headers.size() == 2 && a.headers.at("Content-Type") == "text/plain");
    CHECK(a.body == "first" && a.keep_alive);
    const Message& b = c.messages[1];
    CHECK(b.complete && b.status == 301 && b.reason == "Moved Permanently");
    CHECK(b.headers.size() == 3 && b.headers.at("Location") == "/new");
    CHECK(b.body == "second" && !b.keep_alive);
  }

  const std::string url = "/search?q=cabin#top";
  http_parser_url u;
  http_parser_url_init(&u);
  CHECK(http_parser_parse_url(url.data(), url.size(), 0, &u) == 0);
  CHECK(part(url, u, UF_HOST) == "<unset>");
  CHECK(part(url, u, UF_PATH) == "/search");
  CHECK(part(url, u, UF_QUERY) == "q=cabin");
  CHECK(part(url, u, UF_FRAGMENT) == "top");

  if (failures != 0) return 1;
  std::puts("http-parser 2.9.4: C++ API OK");
  return 0;
}
