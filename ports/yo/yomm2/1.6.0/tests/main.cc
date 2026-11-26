#include <yorel/yomm2/keywords.hpp>

#include <cstdio>
#include <string>
#include <variant>

using namespace yorel::yomm2;

struct Animal {
  virtual ~Animal() = default;
};
struct Dog : Animal {};
struct Bulldog : Dog {};
struct Cat : Animal {};

register_classes(Animal, Dog, Bulldog, Cat);

declare_method(std::string, kick, (virtual_<Animal&>));
define_method(std::string, kick, (Animal&)) { return "ignore"; }
define_method(std::string, kick, (Dog&)) { return "bark"; }
define_method(std::string, kick, (Bulldog& b)) { return next(b) + "+bite"; }

declare_method(std::string, meet, (virtual_<Animal&>, virtual_<Animal&>));
define_method(std::string, meet, (Animal&, Animal&)) { return "ignore"; }
define_method(std::string, meet, (Dog&, Cat&)) { return "chase"; }
define_method(std::string, meet, (Cat&, Dog&)) { return "run"; }

declare_method(std::string, ambig, (virtual_<Animal&>, virtual_<Animal&>));
define_method(std::string, ambig, (Dog&, Animal&)) { return "first"; }
define_method(std::string, ambig, (Animal&, Dog&)) { return "second"; }

declare_method(std::string, only_cat, (virtual_<Animal&>));
define_method(std::string, only_cat, (Cat&)) { return "meow"; }

struct Failure {
  resolution_error::status_type status;
  std::string method;
};

static void on_error(const error_type& e) {
  auto r = std::get_if<resolution_error>(&e);
  throw Failure{r ? r->status : resolution_error::status_type(0),
                r ? std::string(r->method_name) : ""};
}

static int failures;
#define CHECK(c)                                                  \
  do {                                                            \
    if (!(c)) {                                                   \
      std::printf("%s:%d: %s\n", __FILE__, __LINE__, #c);         \
      ++failures;                                                 \
    }                                                             \
  } while (0)

int main() {
  update();
  set_error_handler(on_error);

  Animal an;
  Dog d;
  Bulldog bd;
  Cat c;
  Animal &a = d, &b = c, &g = bd, &p = an;

  CHECK(kick(a) == "bark");
  CHECK(kick(b) == "ignore");
  CHECK(kick(g) == "bark+bite");
  CHECK(kick(p) == "ignore");

  CHECK(meet(a, b) == "chase");
  CHECK(meet(b, a) == "run");
  CHECK(meet(g, b) == "chase");
  CHECK(meet(a, a) == "ignore");

  CHECK(ambig(a, p) == "first");
  CHECK(ambig(p, a) == "second");

  bool caught = false;
  try {
    ambig(a, a);
  } catch (const Failure& f) {
    caught = true;
    CHECK(f.status == resolution_error::ambiguous);
    CHECK(f.method.find("ambig") != std::string::npos);
  }
  CHECK(caught);

  caught = false;
  try {
    only_cat(a);
  } catch (const Failure& f) {
    caught = true;
    CHECK(f.status == resolution_error::no_definition);
    CHECK(f.method.find("only_cat") != std::string::npos);
  }
  CHECK(caught);

  CHECK(only_cat(b) == "meow");
  return failures;
}
