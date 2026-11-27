// Links graphqlpeg alone.
#include <graphqlservice/GraphQLParse.h>
#include <graphqlservice/internal/Grammar.h>

#include <cstdio>
#include <cstdlib>

using namespace graphql;

int main() {
  auto ast = peg::parseString("{ hero { name } }");
  bool ok = ast.root != nullptr && ast.root->children.size() == 1 &&
            ast.root->children[0]->is_type<peg::operation_definition>();

  bool threw = false;
  try {
    auto bad = peg::parseString("{ hero");
  } catch (const peg::parse_error &) {
    threw = true;
  }
  if (!ok || !threw) {
    std::fprintf(stderr, "FAIL: graphqlpeg probe (parsed %d, threw %d)\n", ok, threw);
    return EXIT_FAILURE;
  }
  std::puts("graphqlpeg probe: ok");
  return EXIT_SUCCESS;
}
