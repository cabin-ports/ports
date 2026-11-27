// Links graphqljson alone.
#include <graphqlservice/JSONResponse.h>

#include <cstdio>
#include <cstdlib>
#include <string>

using namespace graphql;

int main() {
  const std::string text = R"({"a":[1,"b",null],"c":false})";
  auto value = response::parseJSON(text);
  bool ok = value.type() == response::Type::Map && value["a"].size() == 3 &&
            response::toJSON(std::move(value)) == text;
  if (!ok) {
    std::fputs("FAIL: graphqljson probe\n", stderr);
    return EXIT_FAILURE;
  }
  std::puts("graphqljson probe: ok");
  return EXIT_SUCCESS;
}
