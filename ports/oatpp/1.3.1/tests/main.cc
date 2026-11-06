#include "oatpp-test/UnitTest.hpp"
#include "oatpp-test/web/ClientServerTestRunner.hpp"
#include "oatpp/core/base/Environment.hpp"
#include "oatpp/core/macro/codegen.hpp"
#include "oatpp/core/macro/component.hpp"
#include "oatpp/encoding/Base64.hpp"
#include "oatpp/network/virtual_/Interface.hpp"
#include "oatpp/network/virtual_/client/ConnectionProvider.hpp"
#include "oatpp/network/virtual_/server/ConnectionProvider.hpp"
#include "oatpp/parser/json/mapping/ObjectMapper.hpp"
#include "oatpp/web/client/ApiClient.hpp"
#include "oatpp/web/client/HttpRequestExecutor.hpp"
#include "oatpp/web/server/HttpConnectionHandler.hpp"
#include "oatpp/web/server/HttpRouter.hpp"
#include "oatpp/web/server/api/ApiController.hpp"

#include <chrono>
#include <cstdio>
#include <thread>

static int failures = 0;

static void check(bool ok, const char* expr, int line) {
  if (!ok) {
    std::fprintf(stderr, "main.cc:%d: CHECK(%s) failed\n", line, expr);
    ++failures;
  }
}

#define CHECK(...) check((__VA_ARGS__), #__VA_ARGS__, __LINE__)

#include OATPP_CODEGEN_BEGIN(DTO)

class Point : public oatpp::DTO {
  DTO_INIT(Point, DTO)
  DTO_FIELD(String, label, "display-name");
  DTO_FIELD(Int32, x);
  DTO_FIELD(Int32, y);
  DTO_FIELD(Vector<String>, tags);
};

#include OATPP_CODEGEN_END(DTO)

#include OATPP_CODEGEN_BEGIN(ApiController)

class Controller : public oatpp::web::server::api::ApiController {
public:
  explicit Controller(const std::shared_ptr<ObjectMapper>& mapper)
    : oatpp::web::server::api::ApiController(mapper) {}

  ENDPOINT("GET", "/hello/{name}", hello, PATH(String, name)) {
    return createResponse(Status::CODE_200, "hello " + name);
  }

  ENDPOINT("POST", "/shift", shift, QUERY(Int32, by),
           BODY_DTO(Object<Point>, point)) {
    point->x = *point->x + *by;
    return createDtoResponse(Status::CODE_200, point);
  }
};

#include OATPP_CODEGEN_END(ApiController)

#include OATPP_CODEGEN_BEGIN(ApiClient)

class Client : public oatpp::web::client::ApiClient {
  API_CLIENT_INIT(Client)
  API_CALL("GET", "/hello/{name}", hello, PATH(String, name))
  API_CALL("POST", "/shift", shift, QUERY(Int32, by),
           BODY_DTO(Object<Point>, point))
  API_CALL("POST", "/shift", shiftBy, QUERY(String, by),
           BODY_DTO(Object<Point>, point))
  API_CALL("GET", "/missing", missing)
};

#include OATPP_CODEGEN_END(ApiClient)

static oatpp::Object<Point> makePoint() {
  auto point = Point::createShared();
  point->label = "origin";
  point->x = -3;
  point->tags = oatpp::Vector<oatpp::String>::createShared();
  point->tags->push_back("a");
  point->tags->push_back("b");
  return point;
}

static void testJson() {
  auto mapper = oatpp::parser::json::mapping::ObjectMapper::createShared();
  oatpp::String json = mapper->writeToString(makePoint());
  CHECK(json == "{\"display-name\":\"origin\",\"x\":-3,\"y\":null,\"tags\":[\"a\",\"b\"]}");

  auto back = mapper->readFromString<oatpp::Object<Point>>(
      "{\"display-name\":\"far\",\"x\":7,\"y\":-9,\"tags\":[\"z\"]}");
  CHECK(back->label == "far");
  CHECK(*back->x == 7 && *back->y == -9);
  CHECK(back->tags->size() == 1 && back->tags[0] == "z");

  bool threw = false;
  try {
    mapper->readFromString<oatpp::Object<Point>>("{\"x\":");
  } catch (const oatpp::parser::ParsingError&) {
    threw = true;
  }
  CHECK(threw);
}

static void testBase64() {
  using oatpp::encoding::Base64;
  CHECK(Base64::encode("hello") == "aGVsbG8=");
  CHECK(Base64::decode("aGVsbG8=") == "hello");
  bool threw = false;
  try {
    Base64::decode("a*b?");
  } catch (const Base64::DecodingError&) {
    threw = true;
  }
  CHECK(threw);
}

// The HTTP server and client talk over oatpp's in-process virtual
// network, which needs no sockets, under oatpp-test's leak-checking
// UnitTest.
struct Components {
  OATPP_CREATE_COMPONENT(std::shared_ptr<oatpp::network::virtual_::Interface>, iface)([] {
    return oatpp::network::virtual_::Interface::obtainShared("port-probe");
  }());

  OATPP_CREATE_COMPONENT(std::shared_ptr<oatpp::network::ServerConnectionProvider>, provider)([] {
    OATPP_COMPONENT(std::shared_ptr<oatpp::network::virtual_::Interface>, iface);
    return oatpp::network::virtual_::server::ConnectionProvider::createShared(iface);
  }());

  OATPP_CREATE_COMPONENT(std::shared_ptr<oatpp::web::server::HttpRouter>, router)([] {
    return oatpp::web::server::HttpRouter::createShared();
  }());

  OATPP_CREATE_COMPONENT(std::shared_ptr<oatpp::network::ConnectionHandler>, handler)([] {
    OATPP_COMPONENT(std::shared_ptr<oatpp::web::server::HttpRouter>, router);
    return oatpp::web::server::HttpConnectionHandler::createShared(router);
  }());
};

class HttpTest : public oatpp::test::UnitTest {
public:
  HttpTest() : UnitTest("HttpTest") {}

  void onRun() override {
    Components components;
    auto mapper = oatpp::parser::json::mapping::ObjectMapper::createShared();
    oatpp::test::web::ClientServerTestRunner runner;
    runner.addController(std::make_shared<Controller>(mapper));

    OATPP_COMPONENT(std::shared_ptr<oatpp::network::virtual_::Interface>, iface);

    runner.run([&] {
      auto connections = oatpp::network::virtual_::client::ConnectionProvider::createShared(iface);
      auto client = Client::createShared(
          oatpp::web::client::HttpRequestExecutor::createShared(connections), mapper);

      auto hello = client->hello("cabin");
      CHECK(hello->getStatusCode() == 200);
      CHECK(hello->readBodyToString() == "hello cabin");

      auto shifted = client->shift(10, makePoint());
      CHECK(shifted->getStatusCode() == 200);
      auto point = shifted->readBodyToDto<oatpp::Object<Point>>(mapper);
      CHECK(point && *point->x == 7 && point->label == "origin");
      CHECK(point && point->tags->size() == 2 && point->tags[1] == "b");

      CHECK(client->shiftBy("ten", makePoint())->getStatusCode() == 400);
      CHECK(client->missing()->getStatusCode() == 404);
    }, std::chrono::minutes(2));

    // A connection thread drops its counted router and error handler
    // just after the handler stops waiting for it; upstream's tests
    // (test/oatpp/web/FullTest.cpp) give it this second before the
    // UnitTest leak check.
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }
};

int main() {
  oatpp::base::Environment::init();
  testJson();
  testBase64();
  OATPP_RUN_TEST(HttpTest);
  CHECK(oatpp::base::Environment::getObjectsCount() == 0);
  oatpp::base::Environment::destroy();
  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("ok");
  return 0;
}
