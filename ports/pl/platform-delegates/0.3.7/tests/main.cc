// Exercises Delegate and MulticastDelegate with free functions, lambdas and
// member functions, checking results, equality, removal and call order.

#include <Platform.Delegates.h>

#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <type_traits>

using Platform::Delegates::Delegate;
using Platform::Delegates::MulticastDelegate;

static int failures = 0;
static std::string trace;

static void check(bool ok, const char *what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

static int twice(int x) {
  trace += 'T';
  return 2 * x;
}

static int thrice(int x) {
  trace += 'H';
  return 3 * x;
}

static void on_free(int x) { trace += "f" + std::to_string(x); }

struct Recorder {
  int base;
  int add(int x) { return base + x; }
  void on(int x) { trace += "m" + std::to_string(base + x); }
};

template <typename F> static bool throws_bad_function_call(F &&f) {
  try {
    f();
  } catch (const std::bad_function_call &) {
    return true;
  }
  return false;
}

static void test_delegate() {
  Delegate<int(int)> d1(twice), d2(twice), d3(thrice);
  check(d1(21) == 42 && d3(5) == 15, "free function results");
  check(d1 == d2 && !(d1 == d3), "free function equality");

  Delegate deduced(twice);
  static_assert(std::is_same<decltype(deduced), Delegate<int(int)>>::value,
                "deduction guide for a function");

  int offset = 7;
  std::function<int(int)> fn = [offset](int x) { return x + offset; };
  Delegate<int(int)> l1(fn), l2(fn);
  Delegate<int(int)> l1_copy = l1;
  check(l1(1) == 8, "lambda result");
  check(l1 == l1_copy && !(l1 == l2), "lambda equality follows the stored function");

  auto rec = std::make_shared<Recorder>(Recorder{40});
  auto other = std::make_shared<Recorder>(Recorder{40});
  Delegate<int(int)> m1(rec, &Recorder::add);
  Delegate m2(rec, &Recorder::add);
  static_assert(std::is_same<decltype(m2), Delegate<int(int)>>::value,
                "deduction guide for a member function");
  check(m1(2) == 42, "member function result");
  check(m1 == m2 && !(m1 == Delegate<int(int)>(other, &Recorder::add)),
        "member function equality compares the object");
  check(!(m1 == d1), "different kinds are unequal");

  Delegate<int(int)> empty;
  check(throws_bad_function_call([&] { empty(1); }), "empty delegate throws");
}

static void test_multicast() {
  MulticastDelegate<void(int)> event;
  auto lambda = std::make_shared<std::function<void(int)>>(
      [](int x) { trace += "l" + std::to_string(x); });
  Delegate<void(int)> member(std::make_shared<Recorder>(Recorder{100}), &Recorder::on);

  event += on_free;
  event += lambda;
  event += member;
  trace.clear();
  event(1);
  check(trace == "m101l1f1", "multicast calls the newest delegate first");

  MulticastDelegate<void(int)> copy(event);
  event -= lambda;
  trace.clear();
  event(2);
  check(trace == "m102f2", "removing the lambda");
  event -= on_free;
  trace.clear();
  event(3);
  check(trace == "m103", "removing the free function");
  event -= member;
  check(throws_bad_function_call([&] { event(4); }), "empty multicast throws");

  trace.clear();
  copy(5);
  check(trace == "m105l5f5", "a copy keeps its delegates");

  MulticastDelegate<int(int)> calc;
  calc += twice;
  calc += thrice;
  calc += twice;
  calc -= twice;
  trace.clear();
  // -= removes the oldest equal entry, leaving thrice then twice.
  check(calc(5) == 15, "multicast returns the oldest delegate's result");
  check(trace == "TH", "removing one of two equal delegates");
}

int main() {
  test_delegate();
  test_multicast();
  if (failures != 0) {
    return 1;
  }
  std::puts("platform-delegates 0.3.7 C++ ok");
  return 0;
}
