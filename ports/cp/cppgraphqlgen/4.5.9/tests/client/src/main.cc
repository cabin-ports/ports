// Links graphqlclient alone.
#include <graphqlservice/GraphQLClient.h>

#include <cstdio>
#include <cstdlib>
#include <string>

using namespace graphql;

int main() {
  response::Value error(response::Type::Map);
  error.emplace_back("message", response::Value("boom"));
  response::Value errors(response::Type::List);
  errors.emplace_back(std::move(error));
  response::Value data(response::Type::Map);
  data.emplace_back("x", response::Value(1));
  response::Value reply(response::Type::Map);
  reply.emplace_back("data", std::move(data));
  reply.emplace_back("errors", std::move(errors));

  auto parsed = client::parseServiceResponse(std::move(reply));
  bool ok = parsed.data["x"].get<int>() == 1 && parsed.errors.size() == 1 &&
            parsed.errors[0].message == "boom";
  if (!ok) {
    std::fputs("FAIL: graphqlclient probe\n", stderr);
    return EXIT_FAILURE;
  }
  std::puts("graphqlclient probe: ok");
  return EXIT_SUCCESS;
}
