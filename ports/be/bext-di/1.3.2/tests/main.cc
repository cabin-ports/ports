// Consumer test for cabin-ports/bext-di: builds object graphs through the
// public injector API and checks every injected value.
#include <boost/di.hpp>
#include <boost/di/extension/injections/factory.hpp>

#include <cstdio>
#include <memory>
#include <string>
#include <utility>

namespace di = boost::di;

namespace {

int failures = 0;

void check(bool pass, const char *what) {
  if (!pass) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

struct greeter {
  virtual ~greeter() noexcept = default;
  virtual std::string greet() const = 0;
};

struct english : greeter {
  std::string greet() const override { return "hello"; }
};

// A two-level graph: app needs a service, which needs a greeter and an int.
struct service {
  service(std::shared_ptr<greeter> g, int retries) : g(std::move(g)), retries(retries) {}
  std::shared_ptr<greeter> g;
  int retries;
};

struct app {
  app(service s, std::string name) : s(std::move(s)), name(std::move(name)) {}
  service s;
  std::string name;
};

void test_graph() {
  auto injector = di::make_injector(di::bind<greeter>.to<english>(), di::bind<int>.to(3),
                                    di::bind<std::string>.to(std::string("demo")));
  auto a = injector.create<app>();
  check(a.s.g != nullptr && a.s.g->greet() == "hello", "the interface is bound to its implementation");
  check(a.s.retries == 3, "the bound int is injected");
  check(a.name == "demo", "the bound string is injected");

  auto p = injector.create<std::unique_ptr<app>>();
  check(p != nullptr && p->s.retries == 3 && p->name == "demo", "create<std::unique_ptr<T>> builds the same graph");
}

// Three ints told apart by name.
auto port_name = [] {};
auto timeout_name = [] {};

struct server {
  BOOST_DI_INJECT(server, (named = port_name) int port, (named = timeout_name) int timeout, int backlog)
      : port(port), timeout(timeout), backlog(backlog) {}
  int port;
  int timeout;
  int backlog;
};

void test_named() {
  auto injector = di::make_injector(di::bind<int>.named(port_name).to(8080), di::bind<int>.named(timeout_name).to(30),
                                    di::bind<int>.to(16));
  auto s = injector.create<server>();
  check(s.port == 8080 && s.timeout == 30 && s.backlog == 16, "named bindings reach their parameters");
}

void test_scopes() {
  auto shared = di::make_injector(di::bind<greeter>.to<english>().in(di::singleton));
  auto a = shared.create<std::shared_ptr<greeter>>();
  auto b = shared.create<std::shared_ptr<greeter>>();
  check(a != nullptr && a == b, "a singleton binding yields one object");

  auto fresh = di::make_injector(di::bind<greeter>.to<english>().in(di::unique));
  auto c = fresh.create<std::shared_ptr<greeter>>();
  auto d = fresh.create<std::shared_ptr<greeter>>();
  check(c != nullptr && d != nullptr && c != d, "a unique binding yields a new object each time");

  auto mine = std::make_shared<english>();
  auto given = di::make_injector(di::bind<greeter>.to(mine));
  check(given.create<std::shared_ptr<greeter>>() == mine, "an instance binding yields that instance");
}

// The factory extension: the int comes from the call, the string from the
// injector.
struct repeater : greeter {
  repeater(int times, std::string word) : times(times), word(std::move(word)) {}
  std::string greet() const override {
    std::string out;
    for (int i = 0; i < times; ++i) {
      out += word;
    }
    return out;
  }
  int times;
  std::string word;
};

struct greetings {
  explicit greetings(const di::extension::ifactory<greeter, int> &make)
      : twice(make.create(2)->greet()), thrice(make.create(3)->greet()) {}
  std::string twice;
  std::string thrice;
};

void test_factory() {
  auto injector = di::make_injector(
      di::bind<std::string>().to(std::string("ab")),
      di::bind<di::extension::ifactory<greeter, int>>().to(di::extension::factory<repeater>{}));
  auto g = injector.create<greetings>();
  check(g.twice == "abab" && g.thrice == "ababab", "the factory extension forwards its argument");
}

} // namespace

int main() {
  test_graph();
  test_named();
  test_scopes();
  test_factory();
  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("bext-di 1.3.2 C++ ok");
  return 0;
}
