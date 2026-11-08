#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

#include <sigc++/sigc++.h>
#include <sigc++config.h>

namespace {

bool ok = true;

void check(bool cond, const char *what) {
  if (!cond) {
    std::printf("FAIL: %s\n", what);
    ok = false;
  }
}

std::string trace;

int add(int a, int b) { return a + b; }
int sub(int a, int b) { return a - b; }
double halve(int x) { return x / 2.0; }
int bumped = 0;
void bump(int n) { bumped += n; }
int thrower(int x) {
  if (x < 0) throw std::runtime_error("negative");
  return x;
}

// Only explicitly constructible from int, so a slot taking int needs retype.
struct Meters {
  explicit Meters(int v) : value(v) {}
  int value;
};
int twice(Meters m) { return 2 * m.value; }

struct Counter : sigc::trackable {
  int hits = 0;
  void hit(int n) { hits += n; }
};

struct Sum {
  template <typename I>
  int operator()(I first, I last) const {
    int sum = 0;
    for (; first != last; ++first) sum += *first;
    return sum;
  }
};

void signals() {
  sigc::signal<int(int, int)> sig;
  check(sig.empty() && sig(1, 2) == 0, "an empty signal returns a default value");
  sigc::connection sum = sig.connect(sigc::ptr_fun(&add));
  sigc::connection product = sig.connect([](int a, int b) { return a * b; });
  check(sig.size() == 2 && sig.emit(3, 4) == 12, "the last slot's result is returned");
  product.block();
  check(product.blocked() && sig(3, 4) == 7, "a blocked slot is skipped");
  product.unblock();
  sum.disconnect();
  check(!sum.connected() && sig.size() == 1 && sig(3, 4) == 12, "a disconnected slot is gone");

  sigc::signal<void()> order;
  order.connect([] { trace += 'b'; });
  order.connect([] { trace += 'c'; });
  order.connect_first([] { trace += 'a'; });
  trace.clear();
  order();
  check(trace == "abc", "slots run in connection order, connect_first first");

  sigc::slot<void()> zero = [] { trace += '0'; };
  sigc::signal<void()> alias = order;
  alias.connect_first(zero);
  trace.clear();
  order();
  check(trace == "0abc" && zero, "a copied signal shares its slots; connect copies an lvalue slot");
  order.block();
  trace.clear();
  order();
  check(order.blocked() && trace.empty(), "signal::block blocks every slot");
  order.unblock();
  trace.clear();
  order();
  check(!order.blocked() && trace == "0abc", "signal::unblock unblocks every slot");
  order.clear();
  check(order.empty() && alias.empty(), "clear removes every slot");

  sigc::signal<int(int)>::accumulated<Sum> total;
  total.connect([](int x) { return x; });
  total.connect([](int x) { return 10 * x; });
  check(total(5) == 55, "an accumulator collects every slot's result");

  sigc::signal<void()> once;
  sigc::connection self;
  self = once.connect([&self] {
    trace += 'x';
    self.disconnect();
  });
  once.connect([] { trace += 'y'; });
  trace.clear();
  once();
  once();
  check(trace == "xyy" && once.size() == 1, "a slot can disconnect itself while the signal runs");
}

void lifetimes() {
  sigc::signal<void(int)> sig;
  auto counter = std::make_unique<Counter>();
  sigc::connection hit = sig.connect(sigc::mem_fun(*counter, &Counter::hit));
  int lambda_hits = 0;
  {
    auto tracked = std::make_unique<Counter>();
    sig.connect(sigc::track_object([&lambda_hits](int n) { lambda_hits += n; }, *tracked));
    sig(2);
    check(sig.size() == 2 && lambda_hits == 2, "a tracked lambda runs while its object lives");
  }
  sig(3);
  check(sig.size() == 1 && lambda_hits == 2, "destroying the tracked object disconnects the lambda");
  check(counter->hits == 5, "the member function slot ran for both emissions");
  counter.reset();
  check(!hit.connected() && sig.empty(), "destroying a trackable disconnects its slots");

  sigc::signal<void()> scoped;
  {
    sigc::scoped_connection guard = scoped.connect([] {});
    check(scoped.size() == 1, "a scoped connection is connected");
  }
  check(scoped.empty(), "a scoped connection disconnects at the end of its scope");

  sigc::connection kept;
  {
    sigc::scoped_connection outer;
    {
      sigc::scoped_connection inner = scoped.connect([] {});
      outer = std::move(inner);
    }
    check(scoped.size() == 1, "moving a scoped connection keeps it connected");
    sigc::scoped_connection moved = std::move(outer);
    kept = moved.release();
  }
  check(kept.connected() && scoped.size() == 1, "release gives up ownership");
}

void adaptors() {
  sigc::slot<int(int)> minus10 = sigc::bind(sigc::ptr_fun(&sub), 10);
  check(minus10(5) == -5, "bind fixes the last argument");
  sigc::slot<int(int)> ten_minus = sigc::bind<0>(sigc::ptr_fun(&sub), 10);
  check(ten_minus(-3) == 13, "bind<0> fixes the first argument");
  sigc::slot<int(const std::string &, int, int)> ignore_name = sigc::hide<0>(sigc::ptr_fun(&sub));
  check(ignore_name("x", 2, 3) == -1, "hide<0> drops the first argument");
  sigc::slot<int(int, int, double)> ignore_last = sigc::hide(sigc::ptr_fun(&sub));
  check(ignore_last(2, 3, 9.5) == -1, "hide drops the last argument");
  sigc::slot<int(int)> retyped = sigc::retype(sigc::ptr_fun(&twice));
  check(retyped(21) == 42, "retype converts the argument explicitly");
  auto truncated = sigc::retype_return<int>(sigc::ptr_fun(&halve));
  static_assert(std::is_same_v<decltype(truncated(9)), int>, "retype_return<int> returns int");
  check(truncated(9) == 4, "retype_return converts the result");
  sigc::slot<std::string(int)> named = sigc::bind_return(sigc::ptr_fun(&bump), std::string("done"));
  check(named(4) == "done" && bumped == 4, "bind_return calls the functor and replaces its result");
  sigc::slot<int(int)> doubled_less_one =
      sigc::compose(sigc::bind(sigc::ptr_fun(&sub), 1), [](int x) { return 2 * x; });
  check(doubled_less_one(4) == 7, "compose passes the getter's result to the setter");
  sigc::slot<int(int)> safe = sigc::exception_catch(sigc::ptr_fun(&thrower), [] { return -1; });
  check(safe(3) == 3 && safe(-3) == -1, "exception_catch turns a throw into a result");
}

}  // namespace

int main() {
  check(SIGCXX_MAJOR_VERSION == 3 && SIGCXX_MINOR_VERSION == 8 && SIGCXX_MICRO_VERSION == 1,
        "sigc++config.h is 3.8.1");
  signals();
  lifetimes();
  adaptors();
  if (!ok) return 1;
  std::puts("libsigcpp: ok");
  return 0;
}
