#include "nuple.h"  // includes luple.h; the headers have no include guards

#include <cstdio>
#include <cstring>
#include <string>

static int failures;

#define CHECK(cond)                                         \
  do {                                                      \
    if (!(cond)) {                                          \
      std::fprintf(stderr, "FAILED: %s\n", #cond);          \
      ++failures;                                           \
    }                                                       \
  } while (0)

int main() {
  using foo_t = luple<int, float, std::string>;
  foo_t f{10, 10.f, "hello"};

  CHECK(get<0>(f) == 10);
  CHECK(get<1>(f) == 10.f);
  CHECK(get<2>(f) == "hello");
  CHECK(get<std::string>(f) == "hello");
  CHECK(get<int>(f) == 10);

  get<float>(f) = 20.f;
  CHECK(get<1>(f) == 20.f);

  CHECK(size(f) == 3);
  CHECK(index<int>(f) == 0);
  CHECK(index<float>(f) == 1);
  CHECK(index<std::string>(f) == 2);
  CHECK(index<char>(f) == -1);

  using person_t = luple<std::string, int>;
  person_t p[] = {{"alex", 0}, {"ivan", 1}};
  CHECK(p[0] < p[1]);
  CHECK(!(p[0] == p[1]));
  CHECK((p[1] == person_t{"ivan", 1}));

  using nameid_t = nuple<$("name"), char const*, $("id"), int>;
  nameid_t n{"john", 3};
  CHECK(std::strcmp(get<$("name")>(n), "john") == 0);
  CHECK(get<$("id")>(n) == 3);
  CHECK(get<0>(n) == get<$("name")>(n));
  CHECK(size(n) == 2);

  get<$("id")>(n) = 4;
  CHECK(get<int>(n) == 4);

  auto q = as_nuple($name("name"), std::string{"irene"}, $name("id"), 7);
  CHECK(get<$("name")>(q) == "irene");
  CHECK(get<$("id")>(q) == 7);

  return failures == 0 ? 0 : 1;
}
