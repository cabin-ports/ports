#include <brynet/base/Buffer.hpp>
#include <brynet/base/Packet.hpp>
#include <brynet/base/Stack.hpp>
#include <brynet/base/Timer.hpp>
#include <brynet/base/WaitGroup.hpp>
#include <brynet/base/crypto/Base64.hpp>
#include <brynet/base/crypto/SHA1.hpp>
#include <brynet/base/endian/Endian.hpp>
#include <brynet/net/http/HttpFormat.hpp>
#include <brynet/net/http/HttpParser.hpp>
#include <brynet/net/http/WebSocketFormat.hpp>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <thread>

using namespace brynet;

static int failures = 0;
#define CHECK(cond)                                                      \
  do {                                                                   \
    if (!(cond)) {                                                       \
      std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      ++failures;                                                        \
    }                                                                    \
  } while (0)

// The values and the 31-byte size of upstream's tests/test_packet.cpp, in both
// byte orders, and the big-endian layout of one value.
static void packet() {
  for (const bool big : {false, true}) {
    base::BigPacket packet(big);
    packet.writeBool(true);
    packet.writeINT8(1);
    packet.writeUINT8(2);
    packet.writeINT16(3);
    packet.writeUINT16(4);
    packet.writeINT32(5);
    packet.writeUINT32(6);
    packet.writeINT64(7);
    packet.writeUINT64(8);
    CHECK(packet.getPos() == 31);

    base::BasePacketReader reader(packet.getData(), packet.getPos(), big);
    CHECK(reader.getLeft() == 31);
    CHECK(reader.readBool() == true);
    CHECK(reader.readINT8() == 1);
    CHECK(reader.readUINT8() == 2);
    CHECK(reader.readINT16() == 3);
    CHECK(reader.readUINT16() == 4);
    CHECK(reader.readINT32() == 5);
    CHECK(reader.readUINT32() == 6);
    CHECK(reader.readINT64() == 7);
    CHECK(reader.readUINT64() == 8);
    CHECK(reader.getLeft() == 0);
    CHECK(reader.currentPos() == 31);

    bool threw = false;
    try {
      reader.readUINT8();
    } catch (const std::out_of_range &) {
      threw = true;
    }
    CHECK(threw);
  }

  base::BigPacket big(true);
  big.writeUINT32(0x01020304);
  CHECK(std::memcmp(big.getData(), "\x01\x02\x03\x04", 4) == 0);
  base::BigPacket little(false);
  little.writeUINT32(0x01020304);
  CHECK(std::memcmp(little.getData(), "\x04\x03\x02\x01", 4) == 0);
}

// Upstream's tests/test_endian.cpp values.
static void endian() {
  using namespace base::endian;
  CHECK(hostToNetwork64(0x1234567812345678, true) == 0x7856341278563412);
  CHECK(hostToNetwork32(0x12345678, true) == 0x78563412);
  CHECK(hostToNetwork16(0x1234, true) == 0x3412);
  CHECK(networkToHost64(0x1234567812345678, true) == 0x7856341278563412);
  CHECK(networkToHost32(0x12345678, true) == 0x78563412);
  CHECK(networkToHost16(0x1234, true) == 0x3412);
  CHECK(hostToNetwork32(0x12345678, false) == 0x12345678);
}

// RFC 4648 section 10 test vectors.
static void base64() {
  using namespace base::crypto;
  const char *plain[] = {"", "f", "fo", "foo", "foob", "fooba", "foobar"};
  const char *coded[] = {"", "Zg==", "Zm8=", "Zm9v", "Zm9vYg==", "Zm9vYmE=", "Zm9vYmFy"};
  for (int i = 0; i < 7; ++i) {
    const std::string in = plain[i];
    CHECK(base64_encode(reinterpret_cast<const unsigned char *>(in.data()),
                        static_cast<unsigned int>(in.size())) == coded[i]);
    CHECK(base64_decode(coded[i]) == in);
  }
}

// FIPS 180-1 appendix A: SHA-1("abc"); RFC 6455 section 1.3: the handshake
// answer for the sample key "dGhlIHNhbXBsZSBub25jZQ==".
static void sha1_and_handshake() {
  CSHA1 sha;
  sha.Update(reinterpret_cast<const unsigned char *>("abc"), 3);
  sha.Final();
  unsigned char digest[20];
  CHECK(sha.GetHash(digest));
  CHECK(std::memcmp(digest,
                    "\xa9\x99\x3e\x36\x47\x06\x81\x6a\xba\x3e\x25\x71\x78\x50\xc2\x6c\x9c\xd0\xd8\x9d",
                    20) == 0);

  CHECK(net::http::WebSocketFormat::wsHandshake("dGhlIHNhbXBsZSBub25jZQ==") ==
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n\r\n");
}

// RFC 6455 section 5.7 examples: an unmasked and a masked "Hello" text frame.
static void websocket_frames() {
  using net::http::WebSocketFormat;
  std::string frame;
  CHECK(WebSocketFormat::wsFrameBuild("Hello", frame));
  CHECK(frame == std::string("\x81\x05Hello", 7));

  const std::string masked("\x81\x85\x37\xfa\x21\x3d\x7f\x9f\x4d\x51\x58", 11);
  std::string payload;
  WebSocketFormat::WebSocketFrameType type;
  size_t size = 0;
  bool fin = false;
  CHECK(WebSocketFormat::wsFrameExtractString(masked, payload, type, size, fin));
  CHECK(payload == "Hello");
  CHECK(type == WebSocketFormat::WebSocketFrameType::TEXT_FRAME);
  CHECK(size == 11);
  CHECK(fin);

  // A built masked frame decodes to its payload; a truncated one is not a frame.
  std::string round;
  CHECK(WebSocketFormat::wsFrameBuild(std::string(200, 'x'), round,
                                      WebSocketFormat::WebSocketFrameType::BINARY_FRAME, true, true));
  std::string back;
  CHECK(WebSocketFormat::wsFrameExtractString(round, back, type, size, fin));
  CHECK(back == std::string(200, 'x'));
  CHECK(type == WebSocketFormat::WebSocketFrameType::BINARY_FRAME);
  CHECK(size == round.size());
  std::string none;
  CHECK(!WebSocketFormat::wsFrameExtractString(round.substr(0, 100), none, type, size, fin));
}

static void http() {
  using namespace net::http;
  HttpQueryParameter query;
  query.add("a", "1");
  query.add("b", "two");
  CHECK(query.getResult() == "a=1&b=two");

  HttpRequest request;
  request.setMethod(HttpRequest::HTTP_METHOD::HTTP_METHOD_POST);
  request.setUrl("/path/x");
  request.setQuery(query.getResult());
  request.setHost("example.test");
  request.setBody(std::string("hello"));
  const std::string text = request.getResult();
  CHECK(text == "POST /path/x?a=1&b=two HTTP/1.1\r\n"
                "Content-Length: 5\r\nHost: example.test\r\n\r\nhello");

  // HTTPParser::tryParse is private (HttpService's), so drive the bundled
  // http_parser it wraps.
  struct Seen {
    std::string url, body;
    int status = 0;
  };
  auto on_url = [](http_parser *p, const char *at, size_t n) {
    static_cast<Seen *>(p->data)->url.append(at, n);
    return 0;
  };
  auto on_body = [](http_parser *p, const char *at, size_t n) {
    static_cast<Seen *>(p->data)->body.append(at, n);
    return 0;
  };
  http_parser_settings settings;
  http_parser_settings_init(&settings);
  settings.on_url = on_url;
  settings.on_body = on_body;

  Seen req;
  http_parser parser;
  http_parser_init(&parser, HTTP_REQUEST);
  parser.data = &req;
  CHECK(http_parser_execute(&parser, &settings, text.data(), text.size()) == text.size());
  CHECK(parser.http_errno == HPE_OK);
  CHECK(parser.method == HTTP_POST);
  CHECK(req.url == "/path/x?a=1&b=two");
  CHECK(req.body == "hello");
  http_parser_url url;
  http_parser_url_init(&url);
  CHECK(http_parser_parse_url(req.url.data(), req.url.size(), 0, &url) == 0);
  CHECK((url.field_set & (1 << UF_PATH)) && (url.field_set & (1 << UF_QUERY)));
  CHECK(req.url.substr(url.field_data[UF_QUERY].off, url.field_data[UF_QUERY].len) == "a=1&b=two");

  HttpResponse response;
  response.setBody(std::string("<html>hi</html>"));
  const std::string reply = response.getResult();
  CHECK(reply == "HTTP/1.1 200 OK\r\nContent-Length: 15\r\n\r\n<html>hi</html>");

  Seen res;
  http_parser answer;
  http_parser_init(&answer, HTTP_RESPONSE);
  answer.data = &res;
  CHECK(http_parser_execute(&answer, &settings, reply.data(), reply.size()) == reply.size());
  CHECK(answer.status_code == 200);
  CHECK(res.body == "<html>hi</html>");

  Seen bad;
  http_parser garbage_parser;
  http_parser_init(&garbage_parser, HTTP_REQUEST);
  garbage_parser.data = &bad;
  const std::string garbage = "NOT AN HTTP REQUEST\r\n\r\n";
  http_parser_execute(&garbage_parser, &settings, garbage.data(), garbage.size());
  CHECK(garbage_parser.http_errno != HPE_OK);
}

static void containers() {
  auto buffer = base::buffer_new(100);
  int value = 5;
  CHECK(base::buffer_getwritevalidcount(buffer) == 100);
  CHECK(base::buffer_write(buffer, reinterpret_cast<const char *>(&value), sizeof(value)));
  CHECK(base::buffer_getreadvalidcount(buffer) == sizeof(value));
  CHECK(std::memcmp(base::buffer_getreadptr(buffer), &value, sizeof(value)) == 0);
  char large[200];
  CHECK(!base::buffer_write(buffer, large, sizeof(large)));
  base::buffer_delete(buffer);

  auto stack = base::stack_new(3, sizeof(size_t));
  for (size_t i = 0; i < 3; ++i) CHECK(base::stack_push(stack, &i));
  CHECK(base::stack_isfull(stack));
  CHECK(*static_cast<size_t *>(base::stack_popfront(stack)) == 0);
  CHECK(*static_cast<size_t *>(base::stack_popback(stack)) == 2);
  CHECK(base::stack_num(stack) == 1);
  base::stack_delete(stack);
}

static void timers_and_threads() {
  auto timers = std::make_shared<base::TimerMgr>();
  int fired = 0;
  timers->addTimer(std::chrono::nanoseconds(0), [&fired]() { ++fired; });
  auto cancelled = timers->addTimer(std::chrono::nanoseconds(0), [&fired]() { fired += 100; });
  timers->addTimer(std::chrono::hours(1), [&fired]() { fired += 10000; });
  cancelled.lock()->cancel();
  timers->schedule();
  CHECK(fired == 1);
  CHECK(!timers->isEmpty());
  timers->clear();
  CHECK(timers->isEmpty());

  auto group = base::WaitGroup::Create();
  int done = 0;
  group->add(2);
  std::thread first([&] { ++done; group->done(); });
  first.join();
  group->wait(std::chrono::milliseconds(10));
  CHECK(done == 1);  // one of the two is still outstanding
  std::thread second([&] { ++done; group->done(); });
  // Bounded: upstream's done() notifies without the waiter's mutex, so a
  // wakeup can be lost; the timeout re-checks the counter.
  group->wait(std::chrono::seconds(30));
  second.join();
  CHECK(done == 2);
}

int main() {
  packet();
  endian();
  base64();
  sha1_and_handshake();
  websocket_frames();
  http();
  containers();
  timers_and_threads();
  if (failures == 0) std::puts("brynet: all checks passed");
  return failures == 0 ? 0 : 1;
}
