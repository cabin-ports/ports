// sordmm.hpp, the header-only C++ wrapper: nodes, statements, find, get,
// and an N-Triples round trip through a string.
#include <sord/sordmm.hpp>

#include <cstdio>
#include <string>

namespace {

int failures = 0;

void check(bool ok, int line, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "main.cc:%d: check failed: %s\n", line, what);
    ++failures;
  }
}

#define CHECK(expr) check((expr), __LINE__, #expr)

}  // namespace

int main() {
  {
    Sord::World world;
    Sord::Model model(world, "http://example.org/");

    Sord::URI a(world, "http://example.org/a");
    Sord::URI b(world, "http://example.org/b");
    Sord::URI p(world, "http://example.org/p");
    Sord::Node n = Sord::Literal::integer(world, 42);

    CHECK(a.is_uri() && !a.is_blank());
    CHECK(a.to_string() == "http://example.org/a");
    CHECK(n.is_int() && n.to_int() == 42);

    model.add_statement(a, p, Sord::Literal(world, "hello"));
    model.add_statement(a, p, n);
    model.add_statement(b, p, a);
    CHECK(model.num_quads() == 3);

    int count = 0;
    for (Sord::Iter i = model.find(a, p, Sord::Node()); !i.end(); ++i) {
      CHECK(i.get_subject() == a);
      ++count;
    }
    CHECK(count == 2);

    Sord::Node got = model.get(b, p, Sord::Node());
    CHECK(got == a);

    const std::string out = model.write_to_string("http://example.org/", SERD_NTRIPLES);
    CHECK(out.find("<http://example.org/b> <http://example.org/p> <http://example.org/a> .") !=
          std::string::npos);
    CHECK(out.find("\"hello\"") != std::string::npos);
    CHECK(out.find("\"42\"^^<http://www.w3.org/2001/XMLSchema#integer>") != std::string::npos);

    Sord::Model loaded(world, "http://example.org/");
    SerdEnv* env = serd_env_new(nullptr);
    loaded.load_string(env, SERD_NTRIPLES, out.c_str(), out.size(), "http://example.org/");
    serd_env_free(env);
    CHECK(loaded.num_quads() == 3);
  }

  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("sord C++ consumer test passed");
  return 0;
}
