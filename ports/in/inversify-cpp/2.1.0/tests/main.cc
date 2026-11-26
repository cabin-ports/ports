// Resolves constant, automatic and dynamic bindings through each scope and
// checks the exact values and the identity of what comes back.
#include <mosure/inversify.hpp>

#include <cstdio>
#include <utility>
#include <memory>

namespace inversify = mosure::inversify;

namespace {

struct IFizz {
  virtual ~IFizz() = default;
  virtual int buzz() = 0;
  int counter = 0;
};

struct Fizz : IFizz {
  Fizz(int foo, double bar) : foo_(foo), bar_(bar) {}
  int buzz() override {
    ++counter;
    return foo_ + static_cast<int>(bar_);
  }
  int foo_;
  double bar_;
};

struct IQux {
  virtual ~IQux() = default;
  virtual int buzz() = 0;
};

struct Qux : IQux {
  explicit Qux(std::shared_ptr<IFizz> fizz) : fizz_(std::move(fizz)) {}
  int buzz() override { return fizz_->buzz() + 5; }
  std::shared_ptr<IFizz> fizz_;
};

using foo = inversify::Symbol<int>;
struct foo2 : inversify::Symbol<int> {};
using bar = inversify::Symbol<double>;
using fizzUnique = inversify::Symbol<std::unique_ptr<IFizz>>;
using fizzShared = inversify::Symbol<std::shared_ptr<IFizz>>;
using quxUnique = inversify::Symbol<std::unique_ptr<IQux>>;
struct dyn2 : inversify::Symbol<std::shared_ptr<IFizz>> {};

bool ok = true;

void check(bool pass, const char* what) {
  if (!pass) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ok = false;
  }
}

}  // namespace

template <>
struct inversify::Injectable<Fizz> : inversify::Inject<foo, bar> {};

template <>
struct inversify::Injectable<Qux> : inversify::Inject<fizzShared> {};

int main() {
  {
    inversify::Container<foo, foo2, bar> c;
    c.bind<foo>().toConstantValue(10);
    c.bind<foo2>().toConstantValue(20);
    c.bind<bar>().toConstantValue(1.5);
    check(c.get<foo>() == 10, "constant foo");
    check(c.get<foo2>() == 20, "constant foo2 is a distinct symbol");
    check(c.get<bar>() == 1.5, "constant double");
    c.bind<foo>().toConstantValue(30);
    check(c.get<foo>() == 30, "rebinding replaces the value");
    check(c.get<foo2>() == 20, "rebinding leaves other symbols");
  }

  {
    inversify::Container<foo, bar, fizzUnique, fizzShared> c;
    c.bind<foo>().toConstantValue(10);
    c.bind<bar>().toConstantValue(2.0);
    c.bind<fizzUnique>().to<Fizz>();
    auto a = c.get<fizzUnique>();
    auto b = c.get<fizzUnique>();
    check(a->buzz() == 12, "automatic injection of constructor arguments");
    check(a != b && b->counter == 0, "unique scope builds a new object");

    c.bind<fizzShared>().to<Fizz>().inSingletonScope();
    auto s1 = c.get<fizzShared>();
    auto s2 = c.get<fizzShared>();
    s1->buzz();
    check(s1 == s2 && s2->counter == 1, "singleton scope shares one object");
  }

  {
    inversify::Container<foo, bar, fizzShared, quxUnique> c;
    c.bind<foo>().toConstantValue(10);
    c.bind<bar>().toConstantValue(1.0);
    c.bind<fizzShared>().to<Fizz>().inSingletonScope();
    c.bind<quxUnique>().to<Qux>();
    auto q = c.get<quxUnique>();
    check(q->buzz() == 16, "nested dependencies resolve through symbols");
    check(c.get<fizzShared>()->counter == 1, "nested dependency is the singleton");
  }

  {
    inversify::Container<foo, dyn2> c;
    c.bind<foo>().toConstantValue(7);
    int calls = 0;
    c.bind<dyn2>().toDynamicValue(
        [&calls](auto& ctx) -> std::shared_ptr<IFizz> {
          ++calls;
          return std::make_shared<Fizz>(ctx.container.template get<foo>(), 0.0);
        });
    check(c.get<dyn2>()->buzz() == 7, "dynamic value sees the container");
    c.get<dyn2>();
    check(calls == 2, "dynamic value runs per resolution");
  }

  if (ok) std::puts("ok");
  return ok ? 0 : 1;
}
