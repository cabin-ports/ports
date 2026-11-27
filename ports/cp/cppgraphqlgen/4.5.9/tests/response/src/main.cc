// Links graphqlresponse alone.
#include <graphqlservice/GraphQLResponse.h>
#include <graphqlservice/internal/Base64.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace graphql;

int main() {
  response::Value map(response::Type::Map);
  map.emplace_back("n", response::Value(7));
  bool ok = map.size() == 1 && map["n"].get<int>() == 7 &&
            !map.emplace_back("n", response::Value(8)) &&
            internal::Base64::toBase64({'H', 'i'}) == "SGk=" &&
            response::IdType{"SGk="} == response::IdType(std::vector<std::uint8_t>{'H', 'i'});
  if (!ok) {
    std::fputs("FAIL: graphqlresponse probe\n", stderr);
    return EXIT_FAILURE;
  }
  std::puts("graphqlresponse probe: ok");
  return EXIT_SUCCESS;
}
