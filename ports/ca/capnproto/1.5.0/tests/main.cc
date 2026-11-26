// kj-test's KJ_MAIN (c++/src/kj/test.c++) supplies main(): it runs every
// KJ_TEST below and exits non-zero when an expectation fails.
#include <capnp/compat/json.h>
#include <capnp/compat/websocket-rpc.h>
#include <capnp/dynamic.h>
#include <capnp/message.h>
#include <capnp/rpc-twoparty.h>
#include <capnp/schema-loader.h>
#include <capnp/schema-parser.h>
#include <capnp/schema.capnp.h>
#include <capnp/schema.h>
#include <capnp/serialize-packed.h>
#include <capnp/serialize-text.h>
#include <capnp/serialize.h>
#include <kj/async-io.h>
#include <kj/compat/gzip.h>
#include <kj/compat/http.h>
#include <kj/compat/url.h>
#include <kj/encoding.h>
#include <kj/filesystem.h>
#include <kj/string.h>
#include <kj/test.h>
#include <kj/vector.h>
#include <kj/compat/tls.h>

namespace {

constexpr char DEMO_CAPNP[] =
    "@0xbf5147cbbecf40c1;\n"
    "struct Point {\n"
    "  x @0 :Int32;\n"
    "  y @1 :Int32;\n"
    "  label @2 :Text;\n"
    "}\n"
    "interface Adder {\n"
    "  add @0 (a :Int32, b :Int32) -> (sum :Int32);\n"
    "}\n";

// demo.capnp, compiled at run time by capnpc's SchemaParser.
struct DemoSchema {
  kj::Own<const kj::Directory> dir = kj::newInMemoryDirectory(kj::nullClock());
  capnp::SchemaParser parser;
  capnp::ParsedSchema file;

  DemoSchema() {
    dir->openFile(kj::Path("demo.capnp"), kj::WriteMode::CREATE)->writeAll(DEMO_CAPNP);
    file = parser.parseFromDirectory(*dir, kj::Path("demo.capnp"), nullptr);
  }
};

class AdderImpl final : public capnp::DynamicCapability::Server {
public:
  explicit AdderImpl(capnp::InterfaceSchema schema) : capnp::DynamicCapability::Server(schema) {}

  kj::Promise<void> call(
      capnp::InterfaceSchema::Method method,
      capnp::CallContext<capnp::DynamicStruct, capnp::DynamicStruct> context) override {
    KJ_REQUIRE(method.getProto().getName() == "add", "unexpected method");
    auto params = context.getParams();
    context.getResults().set("sum",
                             params.get("a").as<int32_t>() + params.get("b").as<int32_t>());
    return kj::READY_NOW;
  }
};

class EchoService final : public kj::HttpService {
public:
  explicit EchoService(const kj::HttpHeaderTable& table) : table(table) {}

  kj::Promise<void> request(kj::HttpMethod method, kj::StringPtr url,
                            const kj::HttpHeaders&, kj::AsyncInputStream&,
                            Response& response) override {
    kj::HttpHeaders responseHeaders(table);
    responseHeaders.set(kj::HttpHeaderId::CONTENT_TYPE, "text/plain");
    auto body = kj::str(method, ' ', url);
    auto stream = response.send(200, "OK", responseHeaders, body.size());
    auto promise = stream->write(body.begin(), body.size());
    return promise.attach(kj::mv(stream), kj::mv(body));
  }

private:
  const kj::HttpHeaderTable& table;
};

void checkNode(capnp::schema::Node::Reader node) {
  KJ_EXPECT(node.getId() == 0x8123456789abcdefull);
  KJ_EXPECT(node.getDisplayName() == "demo.capnp:Point");
  KJ_EXPECT(node.getDisplayNamePrefixLength() == 11u);
  KJ_ASSERT(node.isStruct());
  auto structNode = node.getStruct();
  KJ_EXPECT(structNode.getDataWordCount() == 1u);
  KJ_EXPECT(structNode.getPointerCount() == 1u);
  KJ_ASSERT(structNode.getFields().size() == 2u);
  KJ_EXPECT(structNode.getFields()[0].getName() == "x");
  KJ_EXPECT(structNode.getFields()[1].getName() == "label");
  KJ_EXPECT(structNode.getFields()[1].getCodeOrder() == 1u);
}

}  // namespace

KJ_TEST("kj strings, arrays and base64") {
  KJ_EXPECT(kj::str("capnp ", 1, '.', 5, ' ', true) == "capnp 1.5 true");

  kj::Vector<int> squares;
  for (int i = 1; i <= 4; ++i) squares.add(i * i);
  auto array = squares.releaseAsArray();
  KJ_ASSERT(array.size() == 4u);
  KJ_EXPECT(kj::strArray(array, ",") == "1,4,9,16");

  KJ_EXPECT(kj::encodeBase64(kj::StringPtr("hello").asBytes()) == "aGVsbG8=");
  auto decoded = kj::decodeBase64("aGVsbG8=");
  KJ_EXPECT(!decoded.hadErrors);
  KJ_EXPECT(kj::heapString(decoded.asChars()) == "hello");
}

KJ_TEST("capnp messages round-trip as flat and packed arrays") {
  capnp::MallocMessageBuilder builder;
  auto node = builder.initRoot<capnp::schema::Node>();
  node.setId(0x8123456789abcdefull);
  node.setDisplayName("demo.capnp:Point");
  node.setDisplayNamePrefixLength(11);
  auto structNode = node.initStruct();
  structNode.setDataWordCount(1);
  structNode.setPointerCount(1);
  auto fields = structNode.initFields(2);
  fields[0].setName("x");
  fields[1].setName("label");
  fields[1].setCodeOrder(1);
  checkNode(node.asReader());

  auto words = capnp::messageToFlatArray(builder);
  capnp::FlatArrayMessageReader reader(words);
  KJ_EXPECT(size_t(reader.getEnd() - words.begin()) == words.size());
  checkNode(reader.getRoot<capnp::schema::Node>());

  kj::VectorOutputStream packedOut;
  capnp::writePackedMessage(packedOut, builder);
  auto packed = packedOut.getArray();
  KJ_EXPECT(packed.size() < words.asBytes().size());
  kj::ArrayInputStream packedIn(packed);
  capnp::PackedMessageReader packedReader(packedIn);
  checkNode(packedReader.getRoot<capnp::schema::Node>());
}

KJ_TEST("capnp reflection, dynamic structs and stringification") {
  auto schema = capnp::Schema::from<capnp::schema::Node>();
  KJ_EXPECT(capnp::typeId<capnp::schema::Node>() == 0xe682ab4cf923a417ull);
  KJ_EXPECT(schema.getProto().getId() == 0xe682ab4cf923a417ull);
  KJ_EXPECT(schema.getProto().getDisplayName() == "schema.capnp:Node");
  KJ_EXPECT(schema.getShortDisplayName() == "Node");

  capnp::MallocMessageBuilder builder;
  auto version = builder.initRoot<capnp::schema::CapnpVersion>();
  version.setMajor(1);
  version.setMinor(5);
  KJ_EXPECT(kj::str(version.asReader()) == "(major = 1, minor = 5, micro = 0)");

  capnp::DynamicStruct::Builder dynamic = version;
  dynamic.set("micro", 7);
  KJ_EXPECT(version.getMicro() == 7u);
  KJ_EXPECT(dynamic.get("major").as<uint16_t>() == 1u);

  capnp::SchemaLoader loader;
  loader.loadCompiledTypeAndDependencies<capnp::schema::CapnpVersion>();
  auto loaded = loader.get(capnp::typeId<capnp::schema::CapnpVersion>()).asStruct();
  KJ_EXPECT(loaded.getFields().size() == 3u);
  KJ_EXPECT(loaded.getFieldByName("micro").getIndex() == 2u);
}

KJ_TEST("capnpc parses a schema and its text format") {
  DemoSchema demo;
  KJ_EXPECT(demo.file.getProto().getId() == 0xbf5147cbbecf40c1ull);
  KJ_EXPECT(demo.file.getProto().getDisplayName() == "demo.capnp");
  auto point = demo.file.getNested("Point").asStruct();
  KJ_ASSERT(point.getFields().size() == 3u);
  KJ_EXPECT(point.getFields()[2].getProto().getName() == "label");

  capnp::MallocMessageBuilder builder;
  auto root = builder.initRoot<capnp::DynamicStruct>(point);
  root.set("x", 3);
  root.set("y", -4);
  root.set("label", "origin");

  auto words = capnp::messageToFlatArray(builder);
  capnp::FlatArrayMessageReader reader(words);
  auto read = reader.getRoot<capnp::DynamicStruct>(point);
  KJ_EXPECT(read.get("x").as<int32_t>() == 3);
  KJ_EXPECT(read.get("y").as<int32_t>() == -4);
  KJ_EXPECT(read.get("label").as<capnp::Text>() == "origin");

  capnp::TextCodec text;
  KJ_EXPECT(text.encode(read) == "(x = 3, y = -4, label = \"origin\")");
  capnp::MallocMessageBuilder decoded;
  auto target = decoded.initRoot<capnp::DynamicStruct>(point);
  text.decode("(x = 7, label = \"seven\")", target);
  KJ_EXPECT(target.get("x").as<int32_t>() == 7);
  KJ_EXPECT(target.get("y").as<int32_t>() == 0);
  KJ_EXPECT(target.asReader().get("label").as<capnp::Text>() == "seven");
}

KJ_TEST("capnp-json encodes and decodes structs") {
  capnp::MallocMessageBuilder builder;
  auto version = builder.initRoot<capnp::schema::CapnpVersion>();
  version.setMajor(1);
  version.setMinor(5);

  capnp::JsonCodec json;
  KJ_EXPECT(json.encode(version.asReader()) == "{\"major\":1,\"minor\":5,\"micro\":0}");
  json.setHasMode(capnp::HasMode::NON_DEFAULT);
  KJ_EXPECT(json.encode(version.asReader()) == "{\"major\":1,\"minor\":5}");

  capnp::MallocMessageBuilder decodedMessage;
  auto decoded = decodedMessage.initRoot<capnp::schema::CapnpVersion>();
  json.decode("{\"major\": 2, \"minor\": 7, \"micro\": 9}"_kj, decoded);
  KJ_EXPECT(decoded.getMajor() == 2u);
  KJ_EXPECT(decoded.getMinor() == 7u);
  KJ_EXPECT(decoded.getMicro() == 9u);

  DemoSchema demo;
  capnp::MallocMessageBuilder pointMessage;
  auto point = pointMessage.initRoot<capnp::DynamicStruct>(demo.file.getNested("Point").asStruct());
  point.set("x", -1);
  point.set("label", "p");
  KJ_EXPECT(json.encode(point.asReader()) == "{\"x\":-1,\"label\":\"p\"}");
}

KJ_TEST("capnp-rpc calls a capability over a socket pair") {
  DemoSchema demo;
  auto adder = demo.file.getNested("Adder").asInterface();

  auto io = kj::setupAsyncIo();
  auto pipe = io.provider->newTwoWayPipe();
  capnp::TwoPartyServer server(capnp::DynamicCapability::Client(kj::heap<AdderImpl>(adder)));
  server.accept(kj::mv(pipe.ends[1]));
  capnp::TwoPartyClient client(*pipe.ends[0]);

  auto cap = client.bootstrap().castAs<capnp::DynamicCapability>(adder);
  auto request = cap.newRequest("add");
  request.set("a", 40);
  request.set("b", 2);
  auto response = request.send().wait(io.waitScope);
  KJ_EXPECT(response.get("sum").as<int32_t>() == 42);
}

KJ_TEST("kj-http parses URLs, serves a request and inflates WebSocket frames") {
  auto url = kj::Url::parse("https://capnproto.org/foo/bar?baz=qux#garply");
  KJ_EXPECT(url.scheme == "https");
  KJ_EXPECT(url.host == "capnproto.org");
  KJ_ASSERT(url.path.size() == 2u);
  KJ_EXPECT(url.path[0] == "foo");
  KJ_EXPECT(url.path[1] == "bar");
  KJ_ASSERT(url.query.size() == 1u);
  KJ_EXPECT(url.query[0].name == "baz");
  KJ_EXPECT(url.query[0].value == "qux");
  KJ_EXPECT(KJ_ASSERT_NONNULL(url.fragment) == "garply");
  KJ_EXPECT(kj::str(url) == "https://capnproto.org/foo/bar?baz=qux#garply");

  auto io = kj::setupAsyncIo();
  kj::HttpHeaderTable table;
  EchoService service(table);
  kj::HttpServer server(io.provider->getTimer(), table, service);
  auto pipe = io.provider->newTwoWayPipe();
  auto listenTask = server.listenHttp(kj::mv(pipe.ends[1]));
  auto client = kj::newHttpClient(table, *pipe.ends[0]);
  {
    kj::HttpHeaders headers(table);
    auto request = client->request(kj::HttpMethod::GET, "/greet?name=kj", headers);
    request.body = nullptr;
    auto response = request.response.wait(io.waitScope);
    KJ_EXPECT(response.statusCode == 200u);
    KJ_EXPECT(response.statusText == "OK");
    KJ_EXPECT(KJ_ASSERT_NONNULL(response.headers->get(kj::HttpHeaderId::CONTENT_TYPE)) ==
              "text/plain");
    KJ_EXPECT(response.body->readAllText().wait(io.waitScope) == "GET /greet?name=kj");
  }
  client = nullptr;
  pipe.ends[0]->shutdownWrite();
  listenTask.wait(io.waitScope);

  // RFC 7692 section 7.2.3.1: "Hello" deflated, in two fragments.
  const kj::byte frames[] = {0x41, 0x03, 0xf2, 0x48, 0xcd, 0x80, 0x04, 0xc9, 0xc9, 0x07, 0x00};
  kj::CompressionParameters deflate;
  deflate.outboundMaxWindowBits = size_t(15);
  deflate.inboundMaxWindowBits = size_t(15);
  auto wsPipe = io.provider->newTwoWayPipe();
  auto ws = kj::newWebSocket(kj::mv(wsPipe.ends[1]), nullptr, deflate);
  auto write = wsPipe.ends[0]->write(frames, sizeof(frames));
  auto message = ws->receive().wait(io.waitScope);
  KJ_ASSERT(message.is<kj::String>());
  KJ_EXPECT(message.get<kj::String>() == "Hello");
  write.wait(io.waitScope);
}

KJ_TEST("kj-gzip compresses and inflates") {
  // "foobar" from gzip, as in c++/src/kj/compat/gzip-test.c++.
  const kj::byte foobar[] = {0x1f, 0x8b, 0x08, 0x00, 0xf9, 0x05, 0xb7, 0x59, 0x00,
                             0x03, 0x4b, 0xcb, 0xcf, 0x4f, 0x4a, 0x2c, 0x02, 0x00,
                             0x95, 0x1f, 0xf6, 0x9e, 0x06, 0x00, 0x00, 0x00};
  {
    kj::ArrayInputStream in(foobar);
    kj::GzipInputStream gunzip(in);
    KJ_EXPECT(gunzip.readAllText() == "foobar");
  }

  kj::VectorOutputStream compressed;
  {
    kj::GzipOutputStream gzip(compressed);
    gzip.write("hello hello hello hello", 23);
  }
  auto bytes = compressed.getArray();
  KJ_ASSERT(bytes.size() > 10u);
  KJ_EXPECT(bytes[0] == 0x1f);
  KJ_EXPECT(bytes[1] == 0x8b);
  kj::ArrayInputStream in(bytes);
  kj::GzipInputStream gunzip(in);
  KJ_EXPECT(gunzip.readAllText() == "hello hello hello hello");
}

KJ_TEST("capnp-websocket carries messages over a WebSocket") {
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);
  auto pipe = kj::newWebSocketPipe();
  capnp::WebSocketMessageStream streamA(*pipe.ends[0]);
  capnp::WebSocketMessageStream streamB(*pipe.ends[1]);
  capnp::MessageStream& a = streamA;
  capnp::MessageStream& b = streamB;

  capnp::MallocMessageBuilder builder;
  auto version = builder.initRoot<capnp::schema::CapnpVersion>();
  version.setMajor(1);
  version.setMinor(5);
  auto write = a.writeMessage(builder);
  auto reader = b.readMessage().wait(waitScope);
  write.wait(waitScope);
  auto received = reader->getRoot<capnp::schema::CapnpVersion>();
  KJ_EXPECT(received.getMajor() == 1u);
  KJ_EXPECT(received.getMinor() == 5u);

  auto end = a.end();
  KJ_EXPECT(b.tryReadMessage().wait(waitScope) == nullptr);
  end.wait(waitScope);
}

// kj-tls.  CA_CERT, INTERMEDIATE_CERT, HOST_KEY and VALID_CERT (for
// example.com) are upstream's, from c++/src/kj/compat/tls-test.c++.
static constexpr char CA_CERT[] =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIGMzCCBBugAwIBAgIUDxGXACZeJ0byrswV8gyWskZF2Q8wDQYJKoZIhvcNAQEL\n"
    "BQAwgacxCzAJBgNVBAYTAlVTMRMwEQYDVQQIDApDYWxpZm9ybmlhMRIwEAYDVQQH\n"
    "DAlQYWxvIEFsdG8xFTATBgNVBAoMDFNhbmRzdG9ybS5pbzEbMBkGA1UECwwSVGVz\n"
    "dGluZyBEZXBhcnRtZW50MRcwFQYDVQQDDA5jYS5leGFtcGxlLmNvbTEiMCAGCSqG\n"
    "SIb3DQEJARYTZ2FycGx5QHNhbmRzdG9ybS5pbzAgFw0yMDA2MjcwMDQyNTJaGA8y\n"
    "MTIwMDYwMzAwNDI1MlowgacxCzAJBgNVBAYTAlVTMRMwEQYDVQQIDApDYWxpZm9y\n"
    "bmlhMRIwEAYDVQQHDAlQYWxvIEFsdG8xFTATBgNVBAoMDFNhbmRzdG9ybS5pbzEb\n"
    "MBkGA1UECwwSVGVzdGluZyBEZXBhcnRtZW50MRcwFQYDVQQDDA5jYS5leGFtcGxl\n"
    "LmNvbTEiMCAGCSqGSIb3DQEJARYTZ2FycGx5QHNhbmRzdG9ybS5pbzCCAiIwDQYJ\n"
    "KoZIhvcNAQEBBQADggIPADCCAgoCggIBAKpp8VF/WDPw1V1aD36/uDWI4XRk9OaJ\n"
    "i8tkAbTPutJ7NU4AWv9OzreKIR1PPhj0DtxVOj5KYRTwL1r4DsFWh6D0rBV7oz7o\n"
    "zP8hWznVQBSa2BJ2E4uDD8p5oNz1+O+o4UgSBbOr83Gp5SZGw9KO7cgNql9Id/Ii\n"
    "sHYxXdrYdAl6InuR6q52CJgcGqQgpFYG+KYqDiByfX52slyz5FA/dfZxsmoEVFLB\n"
    "rgbeuhsJGIoasTkGIdCYJhYI7k2uWtvYNurnhgvlpfPHHuSnJ+aWVdKaQUthgbsy\n"
    "T2XHuLYpWx7+B7kCF5B4eKtn3e7yzE7A8jn8Teq6yRNUh1PnM7CRMmz3g4UAxmJT\n"
    "F5NyQd14IqveFuOk40Ba8wLoypll5We5tV7StUyvaOlAhi+gGPHfWKk4MGiBoaOV\n"
    "52D1+/dkh/abokdKZtE59gJX0MrH6mihfc9KQs7N51IhSs4kG5zGIBdvgXtmP17H\n"
    "hixUFi0Y85pqGidW4LLQ1pmK9k0U4gYlwHtqHh8an35/vp/mFhl2BDHcpuYKZ34U\n"
    "ZDo9GglfCTVEsUvAnwfhuYN0e0kveSTuRCMltjGg0Fs1h9ljNNuc46W4qIx/d5ls\n"
    "aMOTKc3PTtwtqgXOXRFn2U7AUXOgtEqyqpuj5ZcjH2YQ3BL24qAiYEHaBOHM+8qF\n"
    "9JLZE64j5dnZAgMBAAGjUzBRMB0GA1UdDgQWBBTmqsbDUpi5hgPbcPESYR9t8jsD\n"
    "7jAfBgNVHSMEGDAWgBTmqsbDUpi5hgPbcPESYR9t8jsD7jAPBgNVHRMBAf8EBTAD\n"
    "AQH/MA0GCSqGSIb3DQEBCwUAA4ICAQADdVBYClYWqNk1s2gamjGsyQ2r88TWTD6X\n"
    "RySVnyQPATWuEctrr6+8qTrbqBP4bTPKE+uTzwk+o5SirdJJAkrcwEsSCFw7J/qf\n"
    "5U/mXN+EUuqyiMHOS/vLe5X1enj0I6fqJY2mCGFD7Jr/+el1XXjzRZsLZHmqSxww\n"
    "T+UjJP+ffvtq3pq4nMQowxXm+Wub0gFHj5wkKMTIDyqnbjzB9bdVd0crtA+EpYIi\n"
    "f8y5WB46g1CngRnMzRQvg5FCmxg57i+mVgiUjUe54VenwK9aeeHIuOdLCZ0RmiNH\n"
    "KHPUBct+S/AXx8DCoAdm51EahwMBnlUyISpwJ+LVMWA2R9DOxdhEF0tv5iBsD9rn\n"
    "oKIWoa0t/Vwnd2n8wyLhuA7N4yzm0rdBjO/rU6n0atIab5+CEDyLeyWQBVwfCUF5\n"
    "XYNxOBJgGfSgJa23KUtn15pS/nSTa6sOtS/Mryc4UuNzxn+3ebNOG4UPlH6miSMK\n"
    "yA+5SCyKgrn3idifzrq+XafA2WUnxdBLgJMM4OIPAGNjCCW2P1cP/NVllUTjTy2y\n"
    "AIKQ/D9V/DzlbIIT6F3CNnqa9xnrBWTKF1YH/zSB7Gh2xlr0WnOWJVQbNUYet982\n"
    "JL5ibRhsiqBgltgQPhKhN/rGuh7Cb28679fQqLXKgOWvV2fC4b2y0v9dG78jGCEE\n"
    "LBzBUUmunw==\n"
    "-----END CERTIFICATE-----\n";

static constexpr char INTERMEDIATE_CERT[] =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIGETCCA/mgAwIBAgICEAAwDQYJKoZIhvcNAQELBQAwgacxCzAJBgNVBAYTAlVT\n"
    "MRMwEQYDVQQIDApDYWxpZm9ybmlhMRIwEAYDVQQHDAlQYWxvIEFsdG8xFTATBgNV\n"
    "BAoMDFNhbmRzdG9ybS5pbzEbMBkGA1UECwwSVGVzdGluZyBEZXBhcnRtZW50MRcw\n"
    "FQYDVQQDDA5jYS5leGFtcGxlLmNvbTEiMCAGCSqGSIb3DQEJARYTZ2FycGx5QHNh\n"
    "bmRzdG9ybS5pbzAgFw0yMDA2MjcwMDQyNTNaGA8yMTIwMDYwMzAwNDI1M1owgZcx\n"
    "CzAJBgNVBAYTAlVTMRMwEQYDVQQIDApDYWxpZm9ybmlhMRUwEwYDVQQKDAxTYW5k\n"
    "c3Rvcm0uaW8xGzAZBgNVBAsMElRlc3RpbmcgRGVwYXJ0bWVudDEbMBkGA1UEAwwS\n"
    "aW50LWNhLmV4YW1wbGUuY29tMSIwIAYJKoZIhvcNAQkBFhNnYXJwbHlAc2FuZHN0\n"
    "b3JtLmlvMIICIjANBgkqhkiG9w0BAQEFAAOCAg8AMIICCgKCAgEAvDRmV4e7F/1K\n"
    "TaySC2xq00aYZfKJmwcvBvLM60xMrwDf5KlHWoYzog72q6qNb0TpvaeOlymU1IfR\n"
    "DkRIMFz6qd4QpK3Ah0vDtqEsoYE6F7gr2QAAaB7HQhczClh6FFkMCygrHDRQlJcF\n"
    "VseTKxnZBUAUhnG7OI8bHMZsprg31SNG1GCXq/CO/rkKIP1Pdwevr8DFHL3LFF03\n"
    "vdeo6+f/3LjlsCzVCNsCcAYIScRSl1scj2QYcwP7tTmDRAmU9EZFv9MWSqRIMT4L\n"
    "/4tP9AL/pWGdx8RRvbXoLJQf+hQW6YnSorRmIH/xYsvqMaan4P0hkomfIHP4nMYa\n"
    "LgI8VsNhTeDZ7IvSF2F73baluTOHhUD5eE/WDffNeslaCoMbH/B3H6ks0zYt/mHG\n"
    "mDaw3OxgMYep7TIE+SABOSJV+pbtQWNyM7u2+TYHm2DaxD3quf+BoYUZT01uDtN4\n"
    "BSsR7XEzF25w/4lDxqBxGAZ0DzItK0kzqMykSWvDIjpSg/UjRj05sc+5zcgE4pX1\n"
    "nOLD+FuB9jVqo6zCiIkHsSI0XHnm4D6awB1UyDwSh8mivfUDT53OpOIwOI3EB/4U\n"
    "iZstUKgyXNrXsE6wS/3JfdDZ9xkw4dWV+0FWKJ4W6Y8UgKvQJRChpCUtcuxfaLjX\n"
    "/ZIcMRYEFNjFppka/7frNT1VNnRvJ9cCAwEAAaNTMFEwHQYDVR0OBBYEFNnHDzWZ\n"
    "NC5pP6njUnf1BMXuQKnnMB8GA1UdIwQYMBaAFOaqxsNSmLmGA9tw8RJhH23yOwPu\n"
    "MA8GA1UdEwEB/wQFMAMBAf8wDQYJKoZIhvcNAQELBQADggIBAG0ShYBIbwGMDUBj\n"
    "9G2vxI4Nx7lip9/wO5NCC2biVxtcjNxwTuCcqCxeSGgdimo82vJTm9Wa1AkoawSv\n"
    "+spoXJAeOL5dGmZX3IRD0KuDoabH7F6xbNOG3HphvwAJcKQOitPAgTt2eGXCaBqw\n"
    "a1ivxPzchHVd3+LyjLMK5G3zzgW0tZhKp7IYE0eGoSGwrw10ox6ibyswe8evul1N\n"
    "r7Z3zFSWqjy6mcyp7PvlImDMYzWHYYdncEaeBHd13TnTQ++AtY3Da2lu7WG5VlcV\n"
    "vcRaNx8ZLIUpW7u76F9fH25GFnQdF87dgt/ufntts4NifwRRRdIDAgunQ0Nf/TDy\n"
    "ow/P7dobloGl9c5xGyXk9EbYYiK/Iuga1yUKJP3UQhfOTNBsDkyUD/jLg+0bI8T+\n"
    "Vv4hKNhzUObXjzA5P7+RoEqnPe8R5wjvea6OJc2MDm5GFrzZOdIEnW/iuaQTE3ol\n"
    "PYZVDvgPB+l7IC0brwTvdnvXaGLFu8ICQtNoOuSkbEAysyBWCEGwloYLL3r/0ozb\n"
    "k3z5mUZVBwhpQS/GEChfUKiLxk9rbDmnhKASa6FVtycSVfoW/Oh5M65ARf9CFIUv\n"
    "cvDYLpQl7Ao8QiDqlQBvitNi665uucsZ/zOdFlTKKAellLASpB/q2Y6J4wkogFNa\n"
    "dGRV0WpQkcuMjXK7bzSSLMo0nMCm\n"
    "-----END CERTIFICATE-----\n";

static constexpr char HOST_KEY[] =
    "-----BEGIN RSA PRIVATE KEY-----\n"
    "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\n"
    "-----END RSA PRIVATE KEY-----\n";

static constexpr char VALID_CERT[] =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIF+jCCA+KgAwIBAgICEAAwDQYJKoZIhvcNAQELBQAwgZcxCzAJBgNVBAYTAlVT\n"
    "MRMwEQYDVQQIDApDYWxpZm9ybmlhMRUwEwYDVQQKDAxTYW5kc3Rvcm0uaW8xGzAZ\n"
    "BgNVBAsMElRlc3RpbmcgRGVwYXJ0bWVudDEbMBkGA1UEAwwSaW50LWNhLmV4YW1w\n"
    "bGUuY29tMSIwIAYJKoZIhvcNAQkBFhNnYXJwbHlAc2FuZHN0b3JtLmlvMCAXDTIw\n"
    "MDYyNzAwNDI1M1oYDzIxMjAwNjI3MDA0MjUzWjCBkDELMAkGA1UEBhMCVVMxEzAR\n"
    "BgNVBAgMCkNhbGlmb3JuaWExFTATBgNVBAoMDFNhbmRzdG9ybS5pbzEbMBkGA1UE\n"
    "CwwSVGVzdGluZyBEZXBhcnRtZW50MRQwEgYDVQQDDAtleGFtcGxlLmNvbTEiMCAG\n"
    "CSqGSIb3DQEJARYTZ2FycGx5QHNhbmRzdG9ybS5pbzCCAiIwDQYJKoZIhvcNAQEB\n"
    "BQADggIPADCCAgoCggIBAMzuvxxieOjyKIGST8X0hOnh7qhNSbwvHmkgjiqam3xr\n"
    "CBz34fHfkxWrQ3uBheAemGGJsaSVDfOcGQvuhkwO8XjPVLm7LIsHwAhP7HuZSQNE\n"
    "km16PUl5BObCPyzOzQOvodz05qC5cHblh4ercB23H3CVLUjkiXpgi1KmTokkPghl\n"
    "tkcxBdNUjP3jpqTfYLZlXow+8DsSLW41/wwxPyCr+sUNhtUWMRKYIApzUI2Y+EdL\n"
    "q6Dei7TO4YkHm7nl7Nyn1SBHBFfaF3039rSOBtzU5/TCVL2UDSDFFTrY5fFJix35\n"
    "N0/R3Hi/0TNLL1iD1jaFCcQOpuLuIPQ9HhA9ltHISnvOrReuUUWB7zzUF2X/rRu8\n"
    "Mr0Sj3j1iJRMyAIF1tlsFxL8xb70Z319ZmHlgOyockWlXAqfR9IuWT9C/BNkbL2H\n"
    "ftkDJs68ZHjNNSjCUkwLVIasnTWh+Xc5afFF1r6ExSwIeBe+ODo3Tsox6qE2YtaI\n"
    "foqrHN6OLVkBd2M6Y4Wqz95p4Bo/mA6f2TehfEHRUFPojRSsemg0YJypvq4tP8kw\n"
    "zENxteG0PRTZi0cAfluVCPTsvd9SccXFuy8Z3Gx4AN9uvvmw9QnS18sTr6RyuNAe\n"
    "X/c7IJvfNMXYwXC69qNJxgLGfVe/WtATao7wwNw45lwRk2MSUqTBn4qT+Smz9oZd\n"
    "AgMBAAGjUzBRMB0GA1UdDgQWBBRdpPVLjMnJFs7lCtMW6x39Wnwt3TAfBgNVHSME\n"
    "GDAWgBTZxw81mTQuaT+p41J39QTF7kCp5zAPBgNVHRMBAf8EBTADAQH/MA0GCSqG\n"
    "SIb3DQEBCwUAA4ICAQA2kid2fGqjeDRVuclfDRr0LhbFYfJJXxW7SPgcUpJYXeAz\n"
    "LXotBm/Cc+K01nNtl0JYfJy4IkaQUYgfVsA5/FqTGnbRmpEd5XidiGE6PfkXZSNj\n"
    "02v6Uv2bAs8NnJirS5F0JhWZ45xAOMbl04QPUdkISF1JzioCcCqWOggbIV7kzwrB\n"
    "MTcsx8vuh04S9vB318pKli4uIjNdwu7HnoqbrqhSgTUK1aXS6sDQNN/nvR6F5TRL\n"
    "MC0cCtIA6n04c09WRfHxl/YPQwayxGD23eQ9UC7Noe/R8B3K/+6XUYIEmXx6HpnP\n"
    "yt/79iBPwLnaVjGDwKfI8EPuSo7AkDSO3uxMjf7eCL2sCzWlgsne9yOfYxGn+q9K\n"
    "h3KTOR1b7EVU/G4h7JxlSHqf3Ii9qFba/HsUo1yMjVEraMNpxXCijGsN30fqUpLg\n"
    "2g9lNKmIdyHuYdlZET082b1dvb7cfYChlqHvrPv5awbsc1Ka2pOFOMwnUi2w3cHc\n"
    "TLq3SyipI+sgJg3HHSJ3zVHrgKUeDoQi52i5WedvIBPfHC4Ik4zEzlkCMDd3hoLe\n"
    "THAsBGBBEICK+lLSo0Mst2kxhTHHK+PxmhorXXnGOpla2wbwzVZgxur45axCWVqH\n"
    "cbdcVhzR2w4XhjaS74WGiHHn5mHb/uYZJiZGpFCNefU2WOBlRCX7hgAzlPa4ZQ==\n"
    "-----END CERTIFICATE-----\n";

KJ_TEST("kj-tls handshakes and checks the host name") {
  auto io = kj::setupAsyncIo();

  kj::TlsKeypair keypair = {kj::TlsPrivateKey(HOST_KEY),
                            kj::TlsCertificate(kj::str(VALID_CERT, INTERMEDIATE_CERT))};
  kj::TlsContext::Options serverOptions;
  serverOptions.defaultKeypair = keypair;
  kj::TlsContext server(kj::mv(serverOptions));

  kj::TlsCertificate ca(CA_CERT);
  kj::TlsContext::Options clientOptions;
  clientOptions.useSystemTrustStore = false;
  clientOptions.trustedCertificates = kj::arrayPtr(&ca, 1);
  kj::TlsContext client(kj::mv(clientOptions));

  {
    auto pipe = io.provider->newTwoWayPipe();
    auto clientPromise = client.wrapClient(kj::mv(pipe.ends[0]), "example.com");
    auto serverPromise = server.wrapServer(kj::mv(pipe.ends[1]));
    auto clientStream = clientPromise.wait(io.waitScope);
    auto serverStream = serverPromise.wait(io.waitScope);

    auto write = clientStream->write("ping", 4);
    char buffer[4];
    serverStream->read(buffer, sizeof(buffer)).wait(io.waitScope);
    write.wait(io.waitScope);
    KJ_EXPECT(kj::heapString(buffer, sizeof(buffer)) == "ping");
  }

  {
    auto pipe = io.provider->newTwoWayPipe();
    auto clientPromise = client.wrapClient(kj::mv(pipe.ends[0]), "wrong.com");
    auto serverPromise = server.wrapServer(kj::mv(pipe.ends[1]));
    KJ_EXPECT_THROW_MESSAGE("ostname mismatch", clientPromise.wait(io.waitScope));
  }
}
