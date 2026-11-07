/* http_parser through its callback API: a chunked request with trailers,
 * fed whole and a byte at a time; a response read to its Content-Length and
 * another to EOF; pause and resume; an upgrade; error reporting; URL
 * parsing; and the non-strict mode this port builds.  C99. */
#include <http_parser.h>

#include <stdio.h>
#include <string.h>

static int failures = 0;

static void check(int ok, int line, const char *what)
{
    if (!ok) {
        fprintf(stderr, "main.c:%d: check failed: %s\n", line, what);
        ++failures;
    }
}

#define CHECK(expr) check((expr) != 0, __LINE__, #expr)

/* What the callbacks saw.  Headers are recorded as "name=value;..." so that
 * fragments split across calls join up. */
struct record {
    char url[64], status[32], headers[160], body[64];
    int begins, headers_done, completes, chunk_headers, chunk_completes;
    unsigned long chunk_sizes[4];
    int in_value;
    int pause_after_headers;
    int fail_url;
};

static void append(char *dst, size_t cap, const char *at, size_t len)
{
    size_t n = strlen(dst);
    if (n + len >= cap) {
        fprintf(stderr, "main.c: record overflow\n");
        ++failures;
        return;
    }
    memcpy(dst + n, at, len);
    dst[n + len] = '\0';
}

#define APPEND(field, at, len) append(field, sizeof field, at, len)

static struct record *rec(http_parser *p)
{
    return (struct record *)p->data;
}

static int on_message_begin(http_parser *p)
{
    rec(p)->begins++;
    return 0;
}

static int on_url(http_parser *p, const char *at, size_t len)
{
    APPEND(rec(p)->url, at, len);
    return rec(p)->fail_url;
}

static int on_status(http_parser *p, const char *at, size_t len)
{
    APPEND(rec(p)->status, at, len);
    return 0;
}

static int on_header_field(http_parser *p, const char *at, size_t len)
{
    struct record *r = rec(p);
    if (r->in_value) {
        APPEND(r->headers, ";", 1);
        r->in_value = 0;
    }
    APPEND(r->headers, at, len);
    return 0;
}

static int on_header_value(http_parser *p, const char *at, size_t len)
{
    struct record *r = rec(p);
    if (!r->in_value) {
        APPEND(r->headers, "=", 1);
        r->in_value = 1;
    }
    APPEND(r->headers, at, len);
    return 0;
}

static int on_headers_complete(http_parser *p)
{
    rec(p)->headers_done++;
    if (rec(p)->pause_after_headers)
        http_parser_pause(p, 1);
    return 0;
}

static int on_body(http_parser *p, const char *at, size_t len)
{
    APPEND(rec(p)->body, at, len);
    return 0;
}

static int on_message_complete(http_parser *p)
{
    rec(p)->completes++;
    rec(p)->in_value = 0;
    return 0;
}

static int on_chunk_header(http_parser *p)
{
    struct record *r = rec(p);
    /* The chunk's size is in content_length during this callback. */
    if (r->chunk_headers < 4)
        r->chunk_sizes[r->chunk_headers] = (unsigned long)p->content_length;
    r->chunk_headers++;
    return 0;
}

static int on_chunk_complete(http_parser *p)
{
    rec(p)->chunk_completes++;
    return 0;
}

static http_parser_settings settings;

static void start(http_parser *p, enum http_parser_type type,
                  struct record *r)
{
    memset(r, 0, sizeof *r);
    p->data = r; /* http_parser_init keeps the data pointer it finds */
    http_parser_init(p, type);
}

static size_t feed(http_parser *p, const char *s)
{
    return http_parser_execute(p, &settings, s, strlen(s));
}

static const char chunked_request[] =
    "POST /upload?id=7 HTTP/1.1\r\n"
    "Host: example.com\r\n"
    "Transfer-Encoding: chunked\r\n"
    "\r\n"
    "5\r\nhello\r\n"
    "6;note=x\r\n world\r\n"
    "0\r\n"
    "Checksum: 42\r\n"
    "\r\n";

static void check_chunked(const http_parser *p, const struct record *r)
{
    CHECK(HTTP_PARSER_ERRNO(p) == HPE_OK);
    CHECK(p->method == HTTP_POST);
    CHECK(p->http_major == 1 && p->http_minor == 1);
    CHECK(strcmp(r->url, "/upload?id=7") == 0);
    CHECK(strcmp(r->headers,
                 "Host=example.com;Transfer-Encoding=chunked;Checksum=42")
          == 0);
    CHECK(strcmp(r->body, "hello world") == 0);
    CHECK(r->begins == 1 && r->headers_done == 1 && r->completes == 1);
    CHECK(r->chunk_headers == 3);
    CHECK(r->chunk_sizes[0] == 5 && r->chunk_sizes[1] == 6 &&
          r->chunk_sizes[2] == 0);
    CHECK(r->chunk_completes == 3);
    CHECK(http_should_keep_alive(p) == 1);
}

static void test_request(void)
{
    http_parser p;
    struct record r;
    size_t i, n = strlen(chunked_request), total = 0;

    start(&p, HTTP_REQUEST, &r);
    CHECK(feed(&p, chunked_request) == n);
    check_chunked(&p, &r);

    start(&p, HTTP_REQUEST, &r);
    for (i = 0; i < n; ++i)
        total += http_parser_execute(&p, &settings, chunked_request + i, 1);
    CHECK(total == n);
    check_chunked(&p, &r);
}

static void test_response(void)
{
    http_parser p;
    struct record r;
    const char *not_found =
        "HTTP/1.0 404 Not Found\r\nContent-Length: 3\r\n\r\nabc";
    const char *eof_body = "HTTP/1.1 200 OK\r\nServer: t\r\n\r\nuntil close";

    start(&p, HTTP_RESPONSE, &r);
    CHECK(feed(&p, not_found) == strlen(not_found));
    CHECK(HTTP_PARSER_ERRNO(&p) == HPE_OK);
    CHECK(p.status_code == 404);
    CHECK(p.http_major == 1 && p.http_minor == 0);
    CHECK(strcmp(r.status, "Not Found") == 0);
    CHECK(strcmp(r.headers, "Content-Length=3") == 0);
    CHECK(strcmp(r.body, "abc") == 0);
    CHECK(r.completes == 1);
    CHECK(http_should_keep_alive(&p) == 0);

    /* No length: the body runs to the end of the stream, which a zero-length
     * execute signals. */
    start(&p, HTTP_RESPONSE, &r);
    CHECK(feed(&p, eof_body) == strlen(eof_body));
    CHECK(r.completes == 0);
    CHECK(http_parser_execute(&p, &settings, NULL, 0) == 0);
    CHECK(HTTP_PARSER_ERRNO(&p) == HPE_OK);
    CHECK(r.completes == 1);
    CHECK(strcmp(r.body, "until close") == 0);
}

static void test_pause_and_upgrade(void)
{
    http_parser p;
    struct record r;
    const char *put = "PUT /f HTTP/1.1\r\nContent-Length: 4\r\n\r\ndata";
    const char *upgrade = "GET /chat HTTP/1.1\r\n"
                          "Connection: Upgrade\r\n"
                          "Upgrade: websocket\r\n"
                          "\r\n"
                          "FRAME";
    size_t n, m;

    start(&p, HTTP_REQUEST, &r);
    r.pause_after_headers = 1;
    n = feed(&p, put);
    CHECK(HTTP_PARSER_ERRNO(&p) == HPE_PAUSED);
    CHECK(n < strlen(put));
    CHECK(r.headers_done == 1 && r.body[0] == '\0');
    /* A paused parser consumes nothing. */
    CHECK(feed(&p, put + n) == 0);
    http_parser_pause(&p, 0);
    r.pause_after_headers = 0;
    m = feed(&p, put + n);
    CHECK(n + m == strlen(put));
    CHECK(HTTP_PARSER_ERRNO(&p) == HPE_OK);
    CHECK(strcmp(r.body, "data") == 0 && r.completes == 1);

    start(&p, HTTP_REQUEST, &r);
    n = feed(&p, upgrade);
    CHECK(p.upgrade == 1);
    CHECK(r.completes == 1);
    CHECK(n == strlen(upgrade) - 5 && strcmp(upgrade + n, "FRAME") == 0);
}

static enum http_errno parse_error(enum http_parser_type type, const char *s)
{
    http_parser p;
    struct record r;
    start(&p, type, &r);
    feed(&p, s);
    return HTTP_PARSER_ERRNO(&p);
}

static void test_errors(void)
{
    http_parser p;
    struct record r;
    char big[120];

    CHECK(parse_error(HTTP_REQUEST, "WHEN /pot HTTP/1.1\r\n\r\n")
          == HPE_INVALID_METHOD);
    CHECK(parse_error(HTTP_REQUEST,
                      "POST / HTTP/1.1\r\nContent-Length: 1\r\n"
                      "Content-Length: 2\r\n\r\n")
          == HPE_UNEXPECTED_CONTENT_LENGTH);
    /* Request smuggling defences: chunked with a length, and a request whose
     * final transfer coding is not chunked. */
    CHECK(parse_error(HTTP_REQUEST,
                      "POST / HTTP/1.1\r\nContent-Length: 5\r\n"
                      "Transfer-Encoding: chunked\r\n\r\n")
          == HPE_UNEXPECTED_CONTENT_LENGTH);
    CHECK(parse_error(HTTP_REQUEST,
                      "POST / HTTP/1.1\r\nTransfer-Encoding: gzip\r\n\r\n")
          == HPE_INVALID_TRANSFER_ENCODING);
    CHECK(parse_error(HTTP_RESPONSE, "HTTP/1.1 2x0 OK\r\n\r\n")
          == HPE_INVALID_STATUS);

    start(&p, HTTP_REQUEST, &r);
    r.fail_url = 1;
    feed(&p, "GET /x HTTP/1.1\r\n\r\n");
    CHECK(HTTP_PARSER_ERRNO(&p) == HPE_CB_url);

    /* The header limit is process-wide and adjustable at run time. */
    memset(big, 'a', sizeof big);
    memcpy(big, "GET / HTTP/1.1\r\nX: ", 19);
    big[sizeof big - 1] = '\0';
    http_parser_set_max_header_size(64);
    CHECK(parse_error(HTTP_REQUEST, big) == HPE_HEADER_OVERFLOW);
    http_parser_set_max_header_size(HTTP_MAX_HEADER_SIZE);
    CHECK(parse_error(HTTP_REQUEST, big) == HPE_OK);

    CHECK(strcmp(http_errno_name(HPE_INVALID_METHOD), "HPE_INVALID_METHOD")
          == 0);
    CHECK(strcmp(http_errno_description(HPE_INVALID_METHOD),
                 "invalid HTTP method") == 0);
}

static int field(const char *url, const struct http_parser_url *u,
                 enum http_parser_url_fields f, const char *want)
{
    size_t len = strlen(want);
    return (u->field_set & (1 << f)) && u->field_data[f].len == len &&
           memcmp(url + u->field_data[f].off, want, len) == 0;
}

static void test_url(void)
{
    struct http_parser_url u;
    const char *full = "http://user:pw@my_host:8080/p/a/t/h?query=1#frag";
    const char *ipv6 = "https://[::1]/";
    const char *authority = "example.com:443";

    http_parser_url_init(&u);
    CHECK(http_parser_parse_url(full, strlen(full), 0, &u) == 0);
    CHECK(field(full, &u, UF_SCHEMA, "http"));
    CHECK(field(full, &u, UF_USERINFO, "user:pw"));
    /* An underscore in a host name passes only in non-strict mode. */
    CHECK(field(full, &u, UF_HOST, "my_host"));
    CHECK(field(full, &u, UF_PORT, "8080") && u.port == 8080);
    CHECK(field(full, &u, UF_PATH, "/p/a/t/h"));
    CHECK(field(full, &u, UF_QUERY, "query=1"));
    CHECK(field(full, &u, UF_FRAGMENT, "frag"));

    http_parser_url_init(&u);
    CHECK(http_parser_parse_url(ipv6, strlen(ipv6), 0, &u) == 0);
    CHECK(field(ipv6, &u, UF_HOST, "::1") && !(u.field_set & (1 << UF_PORT)));

    /* CONNECT targets are host:port and nothing else. */
    http_parser_url_init(&u);
    CHECK(http_parser_parse_url(authority, strlen(authority), 1, &u) == 0);
    CHECK(field(authority, &u, UF_HOST, "example.com") && u.port == 443);
    CHECK(http_parser_parse_url("example.com", 11, 1, &u) != 0);
    CHECK(http_parser_parse_url("http://h:70000/", 15, 0, &u) != 0);
    CHECK(http_parser_parse_url("http://a b/", 11, 0, &u) != 0);
}

/* After a message that closes the connection, a strict parser rejects
 * further data; this non-strict build parses the next message. */
static void test_non_strict(void)
{
    http_parser p;
    struct record r;
    const char *two = "GET /a HTTP/1.0\r\n\r\nGET /b HTTP/1.0\r\n\r\n";

    CHECK(HTTP_PARSER_STRICT == 0);
    start(&p, HTTP_REQUEST, &r);
    CHECK(feed(&p, two) == strlen(two));
    CHECK(HTTP_PARSER_ERRNO(&p) == HPE_OK);
    CHECK(r.completes == 2);
    CHECK(strcmp(r.url, "/a/b") == 0);
}

int main(void)
{
    http_parser_settings_init(&settings);
    settings.on_message_begin = on_message_begin;
    settings.on_url = on_url;
    settings.on_status = on_status;
    settings.on_header_field = on_header_field;
    settings.on_header_value = on_header_value;
    settings.on_headers_complete = on_headers_complete;
    settings.on_body = on_body;
    settings.on_message_complete = on_message_complete;
    settings.on_chunk_header = on_chunk_header;
    settings.on_chunk_complete = on_chunk_complete;

    CHECK(http_parser_version() == 0x020904UL);
    CHECK(strcmp(http_method_str(HTTP_MSEARCH), "M-SEARCH") == 0);
    CHECK(strcmp(http_status_str(HTTP_STATUS_NOT_FOUND), "Not Found") == 0);
    test_request();
    test_response();
    test_pause_and_upgrade();
    test_errors();
    test_url();
    test_non_strict();
    if (failures != 0)
        return 1;
    puts("http-parser 2.9.4: C API OK");
    return 0;
}
