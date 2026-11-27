#include <graphqlservice/GraphQLClient.h>
#include <graphqlservice/GraphQLParse.h>
#include <graphqlservice/GraphQLResponse.h>
#include <graphqlservice/GraphQLService.h>
#include <graphqlservice/JSONResponse.h>
#include <graphqlservice/internal/Base64.h>
#include <graphqlservice/internal/Grammar.h>
#include <graphqlservice/internal/Introspection.h>
#include <graphqlservice/internal/Schema.h>
#include <graphqlservice/internal/Version.h>
#include <graphqlservice/introspection/IntrospectionSchema.h>
#include <graphqlservice/introspection/SchemaObject.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <future>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

using namespace graphql;
using namespace std::literals;

namespace {

int failures = 0;

void check(bool ok, const std::string &what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what.c_str());
    ++failures;
  }
}

void check_equal(const std::string &got, const std::string &want, const char *what) {
  check(got == want, std::string(what) + "\n got: " + got + "\nwant: " + want);
}

// The field names a document selects, depth first.
void collect_fields(const peg::ast_node &node, std::string &out) {
  if (node.is_type<peg::field_name>()) {
    out += node.string_view();
    out += ' ';
  }
  for (const auto &child : node.children) {
    collect_fields(*child, out);
  }
}

// What schemagen generates for `type Query { greeting(name: String!): String! }`,
// written by hand, with the introspection fields wired to graphqlservice's
// introspection objects.
std::shared_ptr<schema::Schema> make_schema() {
  auto schema = std::make_shared<schema::Schema>(false, ""sv);
  introspection::AddTypesToSchema(schema);
  auto query = schema::ObjectType::Make("Query"sv, ""sv);
  schema->AddType("Query"sv, query);
  const auto string =
      schema->WrapType(introspection::TypeKind::NON_NULL, schema->LookupType("String"sv));
  query->AddFields({schema::Field::Make("greeting"sv, ""sv, std::nullopt, string,
                                        {schema::InputValue::Make("name"sv, ""sv, string, ""sv)})});
  schema->AddQueryType(query);
  return schema;
}

class Query final : public service::Object {
public:
  explicit Query(std::shared_ptr<schema::Schema> schema)
      : service::Object(
            {"Query"sv},
            {
                {"greeting"sv,
                 [](service::ResolverParams &&params) {
                   auto name =
                       service::ModifiedArgument<std::string>::require("name", params.arguments);
                   return service::Result<std::string>::convert("Hello, " + name,
                                                                std::move(params));
                 }},
                {"__typename"sv,
                 [](service::ResolverParams &&params) {
                   return service::Result<std::string>::convert("Query"s, std::move(params));
                 }},
                {"__schema"sv,
                 [this](service::ResolverParams &&params) {
                   return service::Result<service::Object>::convert(
                       std::static_pointer_cast<service::Object>(
                           std::make_shared<introspection::object::Schema>(
                               std::make_shared<introspection::Schema>(schema_))),
                       std::move(params));
                 }},
            }),
        schema_(std::move(schema)) {}

private:
  std::shared_ptr<schema::Schema> schema_;
};

class Operations final : public service::Request {
public:
  Operations(std::shared_ptr<Query> query, std::shared_ptr<schema::Schema> schema)
      : service::Request({{service::strQuery, std::move(query)}}, std::move(schema)) {}
};

void test_parse() {
  auto ast = peg::parseString(
      "query Hero($id: ID) { hero(id: $id) { name ...Extra } }\n"
      "fragment Extra on Hero { age }");
  check(ast.root != nullptr && !ast.validated, "parseString result");
  if (ast.root != nullptr) {
    const auto &definitions = ast.root->children;
    check(definitions.size() == 2 && definitions[0]->is_type<peg::operation_definition>() &&
              definitions[1]->is_type<peg::fragment_definition>(),
          "definitions");
    std::string fields;
    collect_fields(*ast.root, fields);
    check_equal(fields, "hero name age ", "field walk");
  }

  auto literal = "{ a b }"_graphql;
  check(literal.root != nullptr && literal.root->children.size() == 1, "_graphql literal");

  std::string error;
  try {
    auto bad = peg::parseString("query { hero(");
  } catch (const peg::parse_error &e) {
    error = e.what();
  }
  check_equal(error, "GraphQL:1:14: Expected https://spec.graphql.org/October2021/#Arguments",
              "malformed query");
}

void test_response() {
  response::Value tags(response::Type::List);
  tags.emplace_back(response::Value("a"));
  tags.emplace_back(response::Value(2));
  tags.emplace_back(response::Value(true));
  tags.emplace_back(response::Value());

  response::Value doc(response::Type::Map);
  check(doc.emplace_back("name", response::Value("Cabin")), "emplace name");
  check(doc.emplace_back("tags", std::move(tags)), "emplace tags");
  check(doc.emplace_back("ratio", response::Value(0.5)), "emplace ratio");
  check(doc.emplace_back("id", response::Value(response::IdType(
                                   std::vector<std::uint8_t>{'f', 'a', 'k', 'e', 'I', 'd'}))),
        "emplace id");
  check(!doc.emplace_back("name", response::Value("again")), "duplicate key refused");
  check(doc.size() == 4 && doc["tags"].size() == 4, "sizes");
  check_equal(response::toJSON(std::move(doc)),
              R"({"name":"Cabin","tags":["a",2,true,null],"ratio":0.5,"id":"ZmFrZUlk"})",
              "toJSON");

  auto parsed = response::parseJSON(R"({"n":3,"f":2.5,"l":[true,null],"s":"café"})");
  check(parsed.type() == response::Type::Map && parsed.size() == 4, "parseJSON map");
  check(parsed["n"].type() == response::Type::Int && parsed["n"].get<int>() == 3, "n");
  check(parsed["f"].type() == response::Type::Float && parsed["f"].get<double>() == 2.5, "f");
  check(parsed["l"].size() == 2 && parsed["l"][0].get<bool>() &&
            parsed["l"][1].type() == response::Type::Null,
        "l");
  check(parsed["s"].get<std::string>() == "caf\xC3\xA9", "s");
  check_equal(response::toJSON(std::move(parsed)),
              "{\"n\":3,\"f\":2.5,\"l\":[true,null],\"s\":\"caf\xC3\xA9\"}", "round trip");

  check_equal(internal::Base64::toBase64({'H', 'i', '!'}), "SGkh", "toBase64");
  check(internal::Base64::fromBase64("SGk=") == std::vector<std::uint8_t>{'H', 'i'},
        "fromBase64");
  check(response::IdType{"ZmFrZUlk"} ==
            response::IdType(std::vector<std::uint8_t>{'f', 'a', 'k', 'e', 'I', 'd'}),
        "IdType from Base64");
}

void test_client() {
  auto reply = client::parseServiceResponse(response::parseJSON(
      R"({"data":{"hero":{"name":"R2-D2"}},)"
      R"("errors":[{"message":"boom","locations":[{"line":2,"column":7}],"path":["hero",0]}]})"));
  check(reply.data["hero"]["name"].get<std::string>() == "R2-D2", "client data");
  check(reply.errors.size() == 1, "client errors");
  if (reply.errors.size() == 1) {
    const auto &error = reply.errors[0];
    check(error.message == "boom", "error message");
    check(error.locations.size() == 1 && error.locations[0].line == 2 &&
              error.locations[0].column == 7,
          "error location");
    check(error.path.size() == 2 && std::get<std::string>(error.path[0]) == "hero" &&
              std::get<int>(error.path[1]) == 0,
          "error path");
  }
}

void test_service() {
  check(internal::FullVersion == "4.5.9"sv, "FullVersion");

  auto schema = make_schema();
  auto service = std::make_shared<Operations>(std::make_shared<Query>(schema), schema);

  auto query =
      peg::parseString(R"({ greeting(name: "Cabin") __typename __schema { queryType { name } } })");
  check(service->validate(query).empty(), "valid query");
  check_equal(
      response::toJSON(service->resolve({query}).get()),
      R"({"data":{"greeting":"Hello, Cabin","__typename":"Query","__schema":{"queryType":{"name":"Query"}}}})",
      "resolve");

  auto greet = peg::parseString("query Greet($who: String!) { greeting(name: $who) }");
  response::Value variables(response::Type::Map);
  variables.emplace_back("who", response::Value("async"));
  check_equal(response::toJSON(
                  service->resolve({greet, "Greet"sv, std::move(variables), std::launch::async})
                      .get()),
              R"({"data":{"greeting":"Hello, async"}})", "resolve with variables, async");

  auto missing = peg::parseString("{ missing }");
  check_equal(
      response::toJSON(service::buildErrorValues(service->validate(missing))),
      R"([{"message":"Undefined field type: Query name: missing","locations":[{"line":1,"column":3}]}])",
      "validation error");
  check_equal(
      response::toJSON(service->resolve({missing}).get()),
      R"({"data":null,"errors":[{"message":"Undefined field type: Query name: missing","locations":[{"line":1,"column":3}]}]})",
      "resolve invalid query");
}

} // namespace

int main() {
  test_parse();
  test_response();
  test_client();
  test_service();

  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return EXIT_FAILURE;
  }
  std::puts("cppgraphqlgen consumer: ok");
  return EXIT_SUCCESS;
}
