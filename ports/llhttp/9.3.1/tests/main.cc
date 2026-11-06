// Consumer test from C++: a message collector over llhttp's extern "C" API,
// fed in uneven pieces, plus an HTTP_BOTH parser that detects responses.
#include <llhttp.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char* what, int line) {
  if (!ok) {
    std::fprintf(stderr, "main.cc:%d: check failed: %s\n", line, what);
    ++failures;
  }
}
#define CHECK(cond) check((cond), #cond, __LINE__)

struct Message {
  std::string url;
  std::vector<std::pair<std::string, std::string>> headers;
  std::string body;
};

struct Collector {
  std::vector<Message> messages;
  Message current;
  bool in_value = false;

  static Collector& of(llhttp_t* p) { return *static_cast<Collector*>(p->data); }

  static int on_url(llhttp_t* p, const char* at, size_t n) {
    of(p).current.url.append(at, n);
    return 0;
  }
  static int on_header_field(llhttp_t* p, const char* at, size_t n) {
    Collector& c = of(p);
    if (c.current.headers.empty() || c.in_value) {
      c.current.headers.emplace_back();
      c.in_value = false;
    }
    c.current.headers.back().first.append(at, n);
    return 0;
  }
  static int on_header_value(llhttp_t* p, const char* at, size_t n) {
    Collector& c = of(p);
    c.in_value = true;
    c.current.headers.back().second.append(at, n);
    return 0;
  }
  static int on_body(llhttp_t* p, const char* at, size_t n) {
    of(p).current.body.append(at, n);
    return 0;
  }
  static int on_message_complete(llhttp_t* p) {
    Collector& c = of(p);
    c.messages.push_back(std::move(c.current));
    c.current = Message();
    c.in_value = false;
    return 0;
  }
};

}  // namespace

int main() {
  llhttp_settings_t settings;
  llhttp_settings_init(&settings);
  settings.on_url = &Collector::on_url;
  settings.on_header_field = &Collector::on_header_field;
  settings.on_header_value = &Collector::on_header_value;
  settings.on_body = &Collector::on_body;
  settings.on_message_complete = &Collector::on_message_complete;

  const std::string input =
      "POST /upload HTTP/1.1\r\n"
      "Content-Type: text/plain\r\n"
      "Content-Length: 11\r\n"
      "\r\n"
      "hello world"
      "DELETE /items/3 HTTP/1.1\r\n"
      "X-Trace: abc\r\n"
      "\r\n";
  Collector collector;
  llhttp_t parser;
  llhttp_init(&parser, HTTP_REQUEST, &settings);
  parser.data = &collector;
  const size_t steps[] = {1, 7, 2, 13, 5};
  for (size_t done = 0, i = 0; done < input.size(); ++i) {
    size_t n = std::min(steps[i % 5], input.size() - done);
    CHECK(llhttp_execute(&parser, input.data() + done, n) == HPE_OK);
    done += n;
  }
  CHECK(llhttp_get_method(&parser) == HTTP_DELETE);
  CHECK(collector.messages.size() == 2);
  if (collector.messages.size() == 2) {
    const Message& post = collector.messages[0];
    CHECK(post.url == "/upload");
    CHECK(post.headers.size() == 2);
    CHECK(post.headers[0] ==
          std::make_pair(std::string("Content-Type"), std::string("text/plain")));
    CHECK(post.headers[1].second == "11");
    CHECK(post.body == "hello world");
    const Message& del = collector.messages[1];
    CHECK(del.url == "/items/3");
    CHECK(del.headers.size() == 1 && del.headers[0].second == "abc");
    CHECK(del.body.empty());
  }

  // HTTP_BOTH settles on the response type from the first bytes.
  Collector response;
  llhttp_init(&parser, HTTP_BOTH, &settings);
  parser.data = &response;
  const char reply[] = "HTTP/1.0 204 No Content\r\n\r\n";
  CHECK(llhttp_execute(&parser, reply, sizeof reply - 1) == HPE_OK);
  CHECK(llhttp_get_type(&parser) == HTTP_RESPONSE);
  CHECK(llhttp_get_status_code(&parser) == 204);
  CHECK(llhttp_get_http_major(&parser) == 1 && llhttp_get_http_minor(&parser) == 0);
  CHECK(response.messages.size() == 1);
  CHECK(std::strcmp(llhttp_errno_name(HPE_PAUSED), "HPE_PAUSED") == 0);

  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("llhttp C++ consumer test passed");
  return 0;
}
