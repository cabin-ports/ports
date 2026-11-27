// Links graphqlservice alone.
#include <graphqlservice/GraphQLService.h>
#include <graphqlservice/internal/Schema.h>
#include <graphqlservice/introspection/IntrospectionSchema.h>

#include <cstdio>
#include <cstdlib>
#include <list>
#include <memory>
#include <string>

using namespace graphql;
using namespace std::literals;

int main() {
  auto schema = std::make_shared<schema::Schema>(false, ""sv);
  introspection::AddTypesToSchema(schema);
  const auto &string = schema->LookupType("String"sv);
  const auto &type = schema->LookupType("__Type"sv);
  bool ok = string != nullptr && string->kind() == introspection::TypeKind::SCALAR &&
            type != nullptr && type->kind() == introspection::TypeKind::OBJECT;

  std::list<service::schema_error> errors;
  errors.push_back({"boom", {2, 7}});
  auto values = service::buildErrorValues(std::move(errors));
  ok = ok && values.type() == response::Type::List && values.size() == 1 &&
       values[0]["message"].get<std::string>() == "boom" &&
       values[0]["locations"][0]["column"].get<int>() == 7;
  if (!ok) {
    std::fputs("FAIL: graphqlservice probe\n", stderr);
    return EXIT_FAILURE;
  }
  std::puts("graphqlservice probe: ok");
  return EXIT_SUCCESS;
}
