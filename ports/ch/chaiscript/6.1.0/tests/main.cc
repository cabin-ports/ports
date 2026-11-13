// chaiscript: C++ functions and a C++ class used from scripts, script values
// and functions used from C++, the JSON helpers, and script errors arriving
// as C++ exceptions.
#include <chaiscript/chaiscript.hpp>

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, int line, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "main.cc:%d: check failed: %s\n", line, what);
    ++failures;
  }
}

#define CHECK(expr) check(static_cast<bool>(expr), __LINE__, #expr)

int mul(int a, int b) { return a * b; }

class Counter {
 public:
  explicit Counter(int start) : value_(start) {}
  int add(int n) { return value_ += n; }
  int value() const { return value_; }

 private:
  int value_;
};

}  // namespace

int main() {
  chaiscript::ChaiScript chai;
  chai.add(chaiscript::fun(&mul), "mul");
  CHECK(chai.eval<int>("var x = mul(6, 7); x") == 42);

  // The script constructs a Counter and also changes one that C++ owns.
  chai.add(chaiscript::user_type<Counter>(), "Counter");
  chai.add(chaiscript::constructor<Counter(int)>(), "Counter");
  chai.add(chaiscript::fun(&Counter::add), "add");
  chai.add(chaiscript::fun(&Counter::value), "value");
  CHECK(chai.eval<int>("var c = Counter(3); c.add(4); c.value()") == 7);
  Counter counter(10);
  chai.add(chaiscript::var(std::ref(counter)), "counter");
  chai.eval("counter.add(5); counter.add(2);");
  CHECK(counter.value() == 17);

  // Script functions, loops, strings and containers.
  const auto square = chai.eval<std::function<int(int)>>("fun(x) { x * x }");
  CHECK(square(9) == 81);
  chai.eval("def fib(n) { if (n < 2) { n } else { fib(n - 1) + fib(n - 2) } }");
  CHECK(chai.eval<int>("fib(10)") == 55);
  CHECK(chai.eval<int>(
            "var total = 0; for (var i = 1; i <= 4; ++i) { total += i; } total") ==
        10);
  CHECK(chai.eval<std::string>("\"chai\" + \"script\"") == "chaiscript");
  const auto tens = chai.eval<std::vector<chaiscript::Boxed_Value>>(
      "map([1, 2, 3], fun(x) { x * 10 })");
  CHECK(tens.size() == 3 && chaiscript::boxed_cast<int>(tens[2]) == 30);

  // to_json and from_json come from utility/json_wrap.hpp.
  CHECK(chai.eval<std::string>(
            "to_json(from_json(\"{\\\"a\\\": [1, 2, \\\"x\\\", true]}\"))") ==
        "{\n  \"a\" : [1, 2, \"x\", true]\n}");

  try {
    chai.eval("mul(\"a\", 2)");
    CHECK(false);
  } catch (const chaiscript::exception::eval_error& e) {
    CHECK(e.reason == "Error with function dispatch with function 'mul'");
  }
  try {
    chai.eval("var = ;");
    CHECK(false);
  } catch (const chaiscript::exception::eval_error& e) {
    CHECK(e.reason == "Incomplete variable declaration" &&
          e.start_position.line == 1 && e.start_position.column == 5);
  }
  try {
    chai.eval<int>("\"text\"");
    CHECK(false);
  } catch (const chaiscript::exception::bad_boxed_cast&) {
  }
  try {
    chai.eval("throw(42)");
    CHECK(false);
  } catch (const chaiscript::Boxed_Value& thrown) {
    CHECK(chaiscript::boxed_cast<int>(thrown) == 42);
  }
  // Module loading is on by default: the loader searches for the module and
  // reports the miss (CHAISCRIPT_NO_DYNLOAD would throw a different message).
  try {
    chai.load_module("cabin_no_such_module");
    CHECK(false);
  } catch (const chaiscript::exception::load_module_error& e) {
    CHECK(std::string(e.what()).rfind(
              "Error loading module 'cabin_no_such_module'", 0) == 0);
  }

  if (failures != 0) {
    return 1;
  }
  std::printf("chaiscript ok\n");
  return 0;
}
