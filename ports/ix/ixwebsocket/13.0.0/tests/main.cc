#include <ixwebsocket/IXBase64.h>
#include <ixwebsocket/IXExponentialBackoff.h>
#include <ixwebsocket/IXGzipCodec.h>
#include <ixwebsocket/IXHttp.h>
#include <ixwebsocket/IXNetSystem.h>
#include <ixwebsocket/IXSelectInterrupt.h>
#include <ixwebsocket/IXSelectInterruptFactory.h>
#include <ixwebsocket/IXSocket.h>
#include <ixwebsocket/IXSocketFactory.h>
#include <ixwebsocket/IXUrlParser.h>
#include <ixwebsocket/IXUserAgent.h>
#include <ixwebsocket/IXUtf8Validator.h>
#include <ixwebsocket/IXUuid.h>
#include <ixwebsocket/IXWebSocketHandshakeKeyGen.h>
#include <ixwebsocket/IXWebSocketHttpHeaders.h>
#include <ixwebsocket/IXWebSocketPerMessageDeflateCodec.h>
#include <ixwebsocket/IXWebSocketPerMessageDeflateOptions.h>
#include <ixwebsocket/IXWebSocketVersion.h>

#include <zlib.h>

#include <cstdio>
#include <string>
#include <tuple>

namespace {

int failures = 0;

void check(bool ok, const char *what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    failures++;
  }
}

std::string bytes(std::initializer_list<unsigned char> b) {
  return std::string(b.begin(), b.end());
}

void url_parser() {
  std::string protocol, host, path, query;
  int port = 0;
  check(ix::UrlParser::parse("wss://google.com/ws?arg=value&arg2=value2",
                             protocol, host, path, query, port),
        "parse wss url");
  check(protocol == "wss" && host == "google.com" &&
            path == "/ws?arg=value&arg2=value2" &&
            query == "arg=value&arg2=value2" && port == 443,
        "wss url components");
  check(ix::UrlParser::parse("http://google.com", protocol, host, path, query,
                             port),
        "parse http url");
  check(protocol == "http" && host == "google.com" && path == "/" &&
            query.empty() && port == 80,
        "http url components");
  bool isDefault = true;
  check(ix::UrlParser::parse("ws://[::1]:9001/chat", protocol, host, path,
                             query, port, isDefault),
        "parse ipv6 url");
  check(protocol == "ws" && host == "::1" && path == "/chat" && port == 9001 &&
            !isDefault,
        "ipv6 url components");
  check(!ix::UrlParser::parse("ws://127.0.0.1:abc/", protocol, host, path,
                              query, port),
        "reject non-numeric port");
  check(!ix::UrlParser::parse("http://example.com:65536/", protocol, host,
                              path, query, port),
        "reject out-of-range port");
  check(ix::UrlParser::isSameOrigin("HTTP://EXAMPLE.com/a",
                                    "http://example.COM:80/b?q"),
        "same origin with default port");
  check(!ix::UrlParser::isSameOrigin("https://example.com",
                                     "http://example.com:443"),
        "different scheme is another origin");
}

// RFC 7692 section 7.2.3: "Hello" as one compressed message, the same
// message again sharing the LZ77 window, and as a stored block.
void per_message_deflate() {
  const std::string hello1 = bytes({0xf2, 0x48, 0xcd, 0xc9, 0xc9, 0x07, 0x00});
  const std::string hello2 = bytes({0xf2, 0x00, 0x11, 0x00, 0x00});
  const std::string stored = bytes(
      {0x00, 0x05, 0x00, 0xfa, 0xff, 0x48, 0x65, 0x6c, 0x6c, 0x6f, 0x00});

  ix::WebSocketPerMessageDeflateCompressor compressor;
  check(compressor.init(15, false), "compressor init");
  std::string out;
  check(compressor.compress(std::string("Hello"), out) && out == hello1,
        "compress first Hello");
  check(compressor.compress(std::string("Hello"), out) && out == hello2,
        "compress second Hello with context takeover");

  ix::WebSocketPerMessageDeflateDecompressor decompressor;
  check(decompressor.init(15, false), "decompressor init");
  check(decompressor.decompress(hello1, out) && out == "Hello",
        "decompress first Hello");
  check(decompressor.decompress(hello2, out) && out == "Hello",
        "decompress second Hello with context takeover");

  ix::WebSocketPerMessageDeflateDecompressor fresh;
  check(fresh.init(15, false), "fresh decompressor init");
  check(fresh.decompress(stored, out) && out == "Hello",
        "decompress stored block");

  std::string text;
  for (int i = 0; i < 100; i++) {
    text += "ixwebsocket ";
  }
  ix::WebSocketPerMessageDeflateCompressor c;
  ix::WebSocketPerMessageDeflateDecompressor d;
  std::string packed, unpacked;
  check(c.init(11, true) && d.init(11, true), "round-trip init");
  check(c.compress(text, packed) && packed.size() < text.size() / 10,
        "compress repeated text");
  check(d.decompress(packed, unpacked) && unpacked == text,
        "round-trip repeated text");
  bool exceeded = false;
  check(!d.decompress(packed, unpacked, 100, &exceeded) && exceeded,
        "decompression output limit");

  // A std::string: a string literal would pick the bool constructor.
  ix::WebSocketPerMessageDeflateOptions options(
      std::string("permessage-deflate; client_max_window_bits=10; "
                  "server_no_context_takeover"));
  check(options.enabled() && options.getServerNoContextTakeover() &&
            !options.getClientNoContextTakeover() &&
            options.getClientMaxWindowBits() == 10 &&
            options.getServerMaxWindowBits() == 15,
        "parse extension header");
  check(options.generateHeader() ==
            "Sec-WebSocket-Extensions: permessage-deflate; "
            "server_no_context_takeover; server_max_window_bits=15; "
            "client_max_window_bits=10\r\n",
        "generate extension header");
}

void gzip() {
  // gzip.compress(b"hello", mtime=0) from Python.
  const std::string hello = bytes(
      {0x1f, 0x8b, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xcb, 0x48,
       0xcd, 0xc9, 0xc9, 0x07, 0x00, 0x86, 0xa6, 0x10, 0x36, 0x05, 0x00, 0x00,
       0x00});
  std::string out;
  check(ix::gzipDecompress(hello, out) && out == "hello", "gunzip hello");

  const std::string text = "hello hello hello hello";
  const std::string z = ix::gzipCompress(text);
  check(z.size() > 18 && z.compare(0, 3, bytes({0x1f, 0x8b, 0x08})) == 0,
        "gzip header");
  std::string back;
  check(ix::gzipDecompress(z, back) && back == text, "gzip round trip");
  std::string bad;
  check(!ix::gzipDecompress("not gzip", bad), "reject non-gzip input");
}

void encodings() {
  check(macaron::Base64::Encode("foobar") == "Zm9vYmFy", "base64 encode");
  check(macaron::Base64::Encode("fo") == "Zm8=", "base64 encode padding");
  std::string decoded;
  check(macaron::Base64::Decode("Zm9vYmE=", decoded).empty() &&
            decoded == "fooba",
        "base64 decode");
  check(!macaron::Base64::Decode("abc", decoded).empty(),
        "base64 rejects bad length");

  // RFC 6455 section 1.3.
  char accept[29] = {};
  WebSocketHandshakeKeyGen::generate("dGhlIHNhbXBsZSBub25jZQ==", accept);
  check(std::string(accept, 28) == "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=",
        "Sec-WebSocket-Accept");

  check(ix::validateUtf8("h\xc3\xa9llo"), "valid utf-8");
  check(!ix::validateUtf8("\xc0\xaf"), "overlong utf-8");
  check(!ix::validateUtf8("\xe2\x82"), "truncated utf-8");
}

void http() {
  auto status = ix::Http::parseStatusLine("HTTP/1.1 404 Not Found");
  check(status.first == "HTTP/1.1" && status.second == 404, "status line");
  auto request = ix::Http::parseRequestLine("GET /index.html HTTP/1.1\r\n");
  check(std::get<0>(request) == "GET" &&
            std::get<1>(request) == "/index.html" &&
            std::get<2>(request) == "HTTP/1.1",
        "request line");

  ix::WebSocketHttpHeaders headers;
  headers["Connection"] = "keep-alive, Upgrade";
  headers["content-type"] = "text/plain";
  check(headers.count("connection") == 1 &&
            headers["CONTENT-TYPE"] == "text/plain" && headers.size() == 2,
        "case-insensitive headers");
  check(ix::hasHttpHeaderToken(headers, "connection", "upgrade") &&
            !ix::hasHttpHeaderToken(headers, "connection", "close"),
        "header token");

  check(ix::calculateRetryWaitMilliseconds(3, 10000, 0) == 800 &&
            ix::calculateRetryWaitMilliseconds(10, 10000, 0) == 10000 &&
            ix::calculateRetryWaitMilliseconds(0, 10000, 500) == 500,
        "exponential backoff");
}

void build_configuration() {
  // The default build: zlib on, TLS off.
  const std::string agent = ix::userAgent();
  const std::string prefix = "ixwebsocket/" IX_WEBSOCKET_VERSION " ";
  const std::string suffix = " nossl zlib " ZLIB_VERSION;
  check(agent.compare(0, prefix.size(), prefix) == 0 &&
            agent.size() > prefix.size() + suffix.size() &&
            agent.compare(agent.size() - suffix.size(), suffix.size(),
                          suffix) == 0,
        "user agent");
  std::string error;
  check(ix::createSocket(true, -1, error, ix::SocketTLSOptions()) == nullptr &&
            error == "TLS support is not enabled on this platform.",
        "no TLS socket");

  const std::string id = ix::uuid4();
  bool hex = id.size() == 32;
  for (char ch : id) {
    hex = hex && ((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'));
  }
  check(hex && id[12] == '4' &&
            std::string("89ab").find(id[16]) != std::string::npos &&
            ix::uuid4() != id,
        "uuid4 format");
}

void platform() {
  // WSAStartup on Windows; the wake-up channel is a pipe on Unix and an
  // event on Windows.
  check(ix::initNetSystem(), "initNetSystem");
  ix::SelectInterruptPtr interrupt = ix::createSelectInterrupt();
  std::string error;
  if (!interrupt || !interrupt->init(error)) {
    check(false, "select interrupt init");
    return;
  }
#ifdef _WIN32
  check(interrupt->getEvent() != nullptr, "select interrupt event");
#else
  check(interrupt->getFd() >= 0, "select interrupt fd");
#endif
  check(interrupt->notify(ix::SelectInterrupt::kSendRequest) &&
            interrupt->notify(ix::SelectInterrupt::kCloseRequest) &&
            interrupt->read() == ix::SelectInterrupt::kSendRequest &&
            interrupt->read() == ix::SelectInterrupt::kCloseRequest &&
            interrupt->read() == 0,
        "select interrupt round trip");
  check(ix::uninitNetSystem(), "uninitNetSystem");
}

}  // namespace

int main() {
  url_parser();
  per_message_deflate();
  gzip();
  encodings();
  http();
  build_configuration();
  platform();
  if (failures != 0) {
    return 1;
  }
  std::puts("ixwebsocket ok");
  return 0;
}
