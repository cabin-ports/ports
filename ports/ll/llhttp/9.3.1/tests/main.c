/* Consumer test: requests and responses parsed through callbacks, whole and
   byte by byte, chunked and EOF-delimited bodies, pipelining and keep-alive,
   errors with their names, reasons and positions, a strict check relaxed by
   a lenient flag, callback errors, pause/resume, and upgrades. */
#include <llhttp.h>

#include <stdio.h>
#include <string.h>

static int failures = 0;

static void check(int ok, const char *what, int line)
{
  if (!ok) {
    fprintf(stderr, "main.c:%d: check failed: %s\n", line, what);
    ++failures;
  }
}
#define CHECK(cond) check((cond) ? 1 : 0, #cond, __LINE__)

typedef struct {
  char url[64];
  char status[32];
  char headers[256]; /* "field=value;" for each header */
  char body[64];
  char keep_alive[8]; /* llhttp_should_keep_alive per message */
  int messages;
  int chunks;
  unsigned long chunk_bytes;
  int pause_on_headers;
  int reject_values;
} capture;

static void append(char *dst, size_t capacity, const char *at, size_t length)
{
  size_t used = strlen(dst);
  if (used + length >= capacity) {
    check(0, "capture buffer overflow", __LINE__);
    return;
  }
  memcpy(dst + used, at, length);
  dst[used + length] = '\0';
}

#define APPEND(field, at, length) \
  append(((capture *)p->data)->field, \
         sizeof ((capture *)p->data)->field, at, length)

static int on_url(llhttp_t *p, const char *at, size_t length)
{
  APPEND(url, at, length);
  return 0;
}

static int on_status(llhttp_t *p, const char *at, size_t length)
{
  APPEND(status, at, length);
  return 0;
}

static int on_header_field(llhttp_t *p, const char *at, size_t length)
{
  APPEND(headers, at, length);
  return 0;
}

static int on_header_field_complete(llhttp_t *p)
{
  APPEND(headers, "=", 1);
  return 0;
}

static int on_header_value(llhttp_t *p, const char *at, size_t length)
{
  if (((capture *)p->data)->reject_values) {
    llhttp_set_error_reason(p, "rejected by callback");
    return HPE_USER;
  }
  APPEND(headers, at, length);
  return 0;
}

static int on_header_value_complete(llhttp_t *p)
{
  APPEND(headers, ";", 1);
  return 0;
}

static int on_headers_complete(llhttp_t *p)
{
  return ((capture *)p->data)->pause_on_headers ? HPE_PAUSED : 0;
}

static int on_body(llhttp_t *p, const char *at, size_t length)
{
  APPEND(body, at, length);
  return 0;
}

static int on_chunk_header(llhttp_t *p)
{
  capture *c = (capture *)p->data;
  ++c->chunks;
  c->chunk_bytes += (unsigned long)p->content_length;
  return 0;
}

static int on_message_complete(llhttp_t *p)
{
  /* The connection flags are reset once the message completes. */
  APPEND(keep_alive, llhttp_should_keep_alive(p) ? "1" : "0", 1);
  ++((capture *)p->data)->messages;
  return 0;
}

static llhttp_settings_t settings;

static void start(llhttp_t *p, capture *c, llhttp_type_t type)
{
  memset(c, 0, sizeof *c);
  llhttp_init(p, type, &settings);
  p->data = c;
}

/* Feeds INPUT STEP bytes at a time and returns the first non-OK errno. */
static llhttp_errno_t feed(llhttp_t *p, const char *input, size_t step)
{
  size_t length = strlen(input), done = 0;
  while (done < length) {
    size_t n = length - done < step ? length - done : step;
    llhttp_errno_t err = llhttp_execute(p, input + done, n);
    if (err != HPE_OK) return err;
    done += n;
  }
  return HPE_OK;
}

static void test_request(size_t step)
{
  static const char input[] =
      "POST /items?id=7 HTTP/1.1\r\n"
      "Host: example.org\r\n"
      /* 16+ bytes of header value take the SIMD scanners' full-block path. */
      "User-Agent: cabin-consumer-test/1.0\r\n"
      "Content-Length: 5\r\n"
      "\r\n"
      "hello";
  llhttp_t p;
  capture c;
  start(&p, &c, HTTP_REQUEST);
  CHECK(feed(&p, input, step) == HPE_OK);
  CHECK(llhttp_get_type(&p) == HTTP_REQUEST);
  CHECK(llhttp_get_method(&p) == HTTP_POST);
  CHECK(strcmp(llhttp_method_name((llhttp_method_t)llhttp_get_method(&p)),
               "POST") == 0);
  CHECK(llhttp_get_http_major(&p) == 1 && llhttp_get_http_minor(&p) == 1);
  CHECK(strcmp(c.url, "/items?id=7") == 0);
  CHECK(strcmp(c.headers,
               "Host=example.org;User-Agent=cabin-consumer-test/1.0;"
               "Content-Length=5;") == 0);
  CHECK(strcmp(c.body, "hello") == 0);
  CHECK(c.messages == 1);
  CHECK(strcmp(c.keep_alive, "1") == 0);
}

static void test_chunked_response(void)
{
  static const char input[] =
      "HTTP/1.1 404 Not Found\r\n"
      "Transfer-Encoding: chunked\r\n"
      "\r\n"
      "5\r\nhello\r\n"
      "6\r\n world\r\n"
      "0\r\n\r\n";
  llhttp_t p;
  capture c;
  start(&p, &c, HTTP_RESPONSE);
  CHECK(feed(&p, input, 3) == HPE_OK);
  CHECK(llhttp_get_status_code(&p) == 404);
  CHECK(strcmp(c.status, "Not Found") == 0);
  CHECK(strcmp(llhttp_status_name(HTTP_STATUS_NOT_FOUND), "NOT_FOUND") == 0);
  CHECK(strcmp(c.body, "hello world") == 0);
  CHECK(c.chunks == 3); /* the zero-length last chunk has a header too */
  CHECK(c.chunk_bytes == 11);
  CHECK(c.messages == 1);
}

static void test_pipelining(void)
{
  static const char input[] =
      "GET /a HTTP/1.1\r\nHost: x\r\n\r\n"
      "GET /b HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n"
      "GET /c HTTP/1.1\r\nHost: x\r\n\r\n";
  llhttp_t p;
  capture c;
  start(&p, &c, HTTP_REQUEST);
  CHECK(feed(&p, input, sizeof input) == HPE_CLOSED_CONNECTION);
  CHECK(c.messages == 2);
  CHECK(strcmp(c.url, "/a/b") == 0);
  CHECK(strcmp(c.keep_alive, "10") == 0);
  /* The lenient flag parses past `Connection: close`. */
  start(&p, &c, HTTP_REQUEST);
  llhttp_set_lenient_keep_alive(&p, 1);
  CHECK(feed(&p, input, sizeof input) == HPE_OK);
  CHECK(c.messages == 3);
  CHECK(strcmp(c.url, "/a/b/c") == 0);
}

static void test_body_until_eof(void)
{
  static const char input[] = "HTTP/1.1 200 OK\r\n\r\nuntil eof";
  llhttp_t p;
  capture c;
  start(&p, &c, HTTP_RESPONSE);
  CHECK(feed(&p, input, sizeof input) == HPE_OK);
  CHECK(llhttp_message_needs_eof(&p) == 1);
  CHECK(c.messages == 0);
  CHECK(llhttp_finish(&p) == HPE_OK);
  CHECK(c.messages == 1);
  CHECK(strcmp(c.body, "until eof") == 0);
}

static int reason_is(const llhttp_t *p, const char *expected)
{
  const char *reason = llhttp_get_error_reason(p);
  return reason != NULL && strcmp(reason, expected) == 0;
}

static void test_errors(void)
{
  static const char bad_method[] = "GE T / HTTP/1.1\r\n\r\n";
  static const char smuggling[] =
      "POST / HTTP/1.1\r\n"
      "Host: x\r\n"
      "Content-Length: 3\r\n"
      "Transfer-Encoding: chunked\r\n"
      "\r\n"
      "0\r\n\r\n";
  llhttp_t p;
  capture c;
  llhttp_errno_t err;

  start(&p, &c, HTTP_REQUEST);
  err = llhttp_execute(&p, bad_method, sizeof bad_method - 1);
  CHECK(err == HPE_INVALID_METHOD);
  CHECK(llhttp_get_errno(&p) == HPE_INVALID_METHOD);
  CHECK(strcmp(llhttp_errno_name(err), "HPE_INVALID_METHOD") == 0);
  CHECK(reason_is(&p, "Invalid method encountered"));
  CHECK(llhttp_get_error_pos(&p) == bad_method + 2);
  /* A non-pause error is sticky until the parser is initialized again. */
  CHECK(llhttp_execute(&p, "GET / HTTP/1.1\r\n\r\n", 18) == HPE_INVALID_METHOD);

  /* Content-Length beside chunked Transfer-Encoding is refused... */
  start(&p, &c, HTTP_REQUEST);
  err = feed(&p, smuggling, sizeof smuggling);
  CHECK(err == HPE_INVALID_TRANSFER_ENCODING);
  CHECK(reason_is(&p, "Transfer-Encoding can't be present with Content-Length"));
  CHECK(c.messages == 0);
  /* ...unless the lenient flag allows it. */
  start(&p, &c, HTTP_REQUEST);
  llhttp_set_lenient_chunked_length(&p, 1);
  CHECK(feed(&p, smuggling, sizeof smuggling) == HPE_OK);
  CHECK(c.messages == 1);

  /* A callback's own error stops parsing with its reason. */
  start(&p, &c, HTTP_REQUEST);
  c.reject_values = 1;
  CHECK(feed(&p, "GET / HTTP/1.1\r\nHost: x\r\n\r\n", 64) == HPE_USER);
  CHECK(reason_is(&p, "rejected by callback"));
  CHECK(c.messages == 0);
}

static void test_pause_and_upgrade(void)
{
  static const char request[] =
      "PUT /f HTTP/1.1\r\nContent-Length: 4\r\n\r\ndata";
  static const char upgrade[] =
      "GET /chat HTTP/1.1\r\n"
      "Host: x\r\n"
      "Connection: Upgrade\r\n"
      "Upgrade: websocket\r\n"
      "\r\n"
      "RAW";
  llhttp_t p;
  capture c;
  const char *pos;

  start(&p, &c, HTTP_REQUEST);
  c.pause_on_headers = 1;
  CHECK(llhttp_execute(&p, request, sizeof request - 1) == HPE_PAUSED);
  CHECK(c.body[0] == '\0');
  pos = llhttp_get_error_pos(&p);
  CHECK(strcmp(pos, "data") == 0);
  llhttp_resume(&p);
  CHECK(llhttp_execute(&p, pos, strlen(pos)) == HPE_OK);
  CHECK(strcmp(c.body, "data") == 0);
  CHECK(c.messages == 1);

  start(&p, &c, HTTP_REQUEST);
  CHECK(llhttp_execute(&p, upgrade, sizeof upgrade - 1) == HPE_PAUSED_UPGRADE);
  CHECK(llhttp_get_upgrade(&p) == 1);
  CHECK(c.messages == 1);
  CHECK(strcmp(llhttp_get_error_pos(&p), "RAW") == 0);
}

int main(void)
{
  llhttp_settings_init(&settings);
  settings.on_url = on_url;
  settings.on_status = on_status;
  settings.on_header_field = on_header_field;
  settings.on_header_field_complete = on_header_field_complete;
  settings.on_header_value = on_header_value;
  settings.on_header_value_complete = on_header_value_complete;
  settings.on_headers_complete = on_headers_complete;
  settings.on_body = on_body;
  settings.on_chunk_header = on_chunk_header;
  settings.on_message_complete = on_message_complete;

  CHECK(LLHTTP_VERSION_MAJOR == 9 && LLHTTP_VERSION_MINOR == 3 &&
        LLHTTP_VERSION_PATCH == 1);
  test_request((size_t)-1);
  test_request(1);
  test_chunked_response();
  test_pipelining();
  test_body_until_eof();
  test_errors();
  test_pause_and_upgrade();
  if (failures != 0) {
    fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  puts("llhttp consumer test passed");
  return 0;
}
