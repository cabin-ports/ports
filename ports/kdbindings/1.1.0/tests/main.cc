// KDBindings from C++17: signals and their connection handles, blockers,
// scoped and reflective connections, deferred connections through a
// ConnectionEvaluator, properties and their change signals, and immediate
// and lazily evaluated bindings, including a user-declared binding function.
#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <kdbindings/binding.h>
#include <kdbindings/property.h>
#include <kdbindings/signal.h>

// Upstream's CMake target defines this for its consumers by default
// (src/kdbindings/CMakeLists.txt:36-38), marking connect() [[nodiscard]].
#ifndef KDBINDINGS_ENABLE_WARN_UNUSED
#error "KDBINDINGS_ENABLE_WARN_UNUSED does not reach consumers"
#endif

namespace {

int failures = 0;

void check(bool ok, int line, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "main.cc:%d: check failed: %s\n", line, what);
    ++failures;
  }
}

#define CHECK(expr) check((expr), __LINE__, #expr)

// Slots run in no documented order, so calls are compared as sorted lists.
std::vector<std::string> sorted(std::vector<std::string> calls) {
  std::sort(calls.begin(), calls.end());
  return calls;
}

struct node_paraboloid {
  template <typename T>
  auto operator()(T&& x, T&& y) const {
    return x * x + y * y;
  }
};

}  // namespace

// Declared outside namespace KDBindings, as upstream's tests do.
KDBINDINGS_DECLARE_FUNCTION(paraboloid, node_paraboloid{})

namespace {

struct Recorder {
  std::string label;
  std::vector<std::string>* log;

  void on(int value, const std::string& text) {
    log->push_back(label + ":" + std::to_string(value) + ":" + text);
  }
};

void signals() {
  KDBindings::Signal<int, std::string> signal;
  std::vector<std::string> log;

  KDBindings::ConnectionHandle direct = signal.connect(
      [&log](int value, const std::string& text) {
        log.push_back("direct:" + std::to_string(value) + ":" + text);
      });
  // The template overload binds a member function to its object.
  Recorder bound_recorder{"bound", &log};
  KDBindings::ConnectionHandle bound =
      signal.connect(&Recorder::on, &bound_recorder);
  CHECK(direct.isActive() && bound.isActive());
  CHECK(direct.belongsTo(signal));

  // It also drops trailing signal arguments a slot does not take.
  int nullary_calls = 0;
  KDBindings::ConnectionHandle nullary =
      signal.connect([&nullary_calls] { ++nullary_calls; });
  // And binds a leading value argument.
  int offset_sum = 0;
  KDBindings::ConnectionHandle offset = signal.connect(
      [&offset_sum](int base, int value) { offset_sum = base + value; }, 100);

  signal.emit(1, "a");
  CHECK(sorted(log) == std::vector<std::string>({"bound:1:a", "direct:1:a"}));
  CHECK(nullary_calls == 1);
  CHECK(offset_sum == 101);
  nullary.disconnect();
  offset.disconnect();

  log.clear();
  CHECK(!signal.blockConnection(bound, true));
  CHECK(bound.isBlocked());
  signal.emit(2, "b");
  CHECK(log == std::vector<std::string>({"direct:2:b"}));
  CHECK(bound.block(false));

  log.clear();
  {
    KDBindings::ConnectionBlocker blocker(direct);
    CHECK(direct.isBlocked());
    signal.emit(3, "c");
  }
  CHECK(!direct.isBlocked());
  CHECK(log == std::vector<std::string>({"bound:3:c"}));

  log.clear();
  direct.disconnect();
  CHECK(!direct.isActive());
  signal.emit(4, "d");
  CHECK(log == std::vector<std::string>({"bound:4:d"}));

  // A scoped connection disconnects when it goes out of scope.
  log.clear();
  {
    Recorder scoped_recorder{"scoped", &log};
    KDBindings::ScopedConnection scoped =
        signal.connect(&Recorder::on, &scoped_recorder);
    signal.emit(5, "e");
  }
  signal.emit(6, "f");
  CHECK(sorted(log) == std::vector<std::string>(
                           {"bound:5:e", "bound:6:f", "scoped:5:e"}));

  // A reflective slot receives its own handle and disconnects itself.
  int reflective_calls = 0;
  KDBindings::ConnectionHandle once = signal.connectReflective(
      [&reflective_calls](KDBindings::ConnectionHandle& self, int,
                          const std::string&) {
        ++reflective_calls;
        self.disconnect();
      });
  signal.emit(7, "g");
  signal.emit(8, "h");
  CHECK(reflective_calls == 1);
  CHECK(!once.isActive());

  // A destroyed signal leaves its handles inactive.
  KDBindings::ConnectionHandle orphan;
  {
    KDBindings::Signal<> temporary;
    orphan = temporary.connect([] {});
    CHECK(orphan.isActive());
  }
  CHECK(!orphan.isActive());

  signal.disconnectAll();
  CHECK(!bound.isActive());
  log.clear();
  signal.emit(9, "i");
  CHECK(log.empty());
}

class CountingEvaluator : public KDBindings::ConnectionEvaluator {
 public:
  int added = 0;

 protected:
  void onInvocationAdded() override { ++added; }
};

void deferred() {
  auto evaluator = std::make_shared<CountingEvaluator>();
  KDBindings::Signal<int> signal;
  std::vector<int> seen;

  KDBindings::ConnectionHandle kept = signal.connectDeferred(
      evaluator, [&seen](int value) { seen.push_back(value); });
  KDBindings::ConnectionHandle dropped = signal.connectDeferred(
      evaluator, [&seen](int value) { seen.push_back(-value); });

  signal.emit(1);
  signal.emit(2);
  CHECK(seen.empty());
  CHECK(evaluator->added == 4);

  // Disconnecting removes that connection's queued invocations.
  dropped.disconnect();
  evaluator->evaluateDeferredConnections();
  CHECK(seen == std::vector<int>({1, 2}));

  seen.clear();
  evaluator->evaluateDeferredConnections();
  CHECK(seen.empty());
  CHECK(kept.isActive());
}

void properties() {
  KDBindings::Property<int> value{5};
  std::vector<std::string> log;
  KDBindings::ConnectionHandle about = value.valueAboutToChange().connect(
      [&log](const int& from, const int& to) {
        log.push_back("about:" + std::to_string(from) + "->" +
                      std::to_string(to));
      });
  KDBindings::ConnectionHandle changed =
      value.valueChanged().connect([&log](const int& now) {
        log.push_back("changed:" + std::to_string(now));
      });

  value = 7;
  CHECK(value.get() == 7);
  CHECK(log == std::vector<std::string>({"about:5->7", "changed:7"}));

  // An equal value is neither stored again nor signalled.
  log.clear();
  value.set(7);
  CHECK(log.empty());
}

void immediate_bindings() {
  const int bytes_per_pixel = 4;
  KDBindings::Property<int> width{800};
  KDBindings::Property<int> height{600};
  // A property constructed from a binding compares its uninitialized value
  // with the first result (property.h:237-241, :265, :362-372), so scalars are
  // initialized first and then given their binding.
  KDBindings::Property<int> size{0};
  size = KDBindings::makeBinding(bytes_per_pixel * width * height);
  CHECK(size.get() == 4 * 800 * 600);

  std::vector<int> sizes;
  KDBindings::ConnectionHandle handle = size.valueChanged().connect(
      [&sizes](const int& now) { sizes.push_back(now); });
  width = 1920;
  height = 1080;
  CHECK(size.get() == 4 * 1920 * 1080);
  CHECK(sizes == std::vector<int>({4 * 1920 * 600, 4 * 1920 * 1080}));

  // A bound property is read-only.
  bool read_only = false;
  try {
    size = 1;
  } catch (const KDBindings::ReadOnlyProperty&) {
    read_only = true;
  }
  CHECK(read_only);
  CHECK(size.get() == 4 * 1920 * 1080);

  // A binding follows a source property that is moved.
  KDBindings::Property<int> moved_width = std::move(width);
  moved_width = 640;
  CHECK(size.get() == 4 * 640 * 1080);

  // reset() detaches the binding and keeps the value.
  size.reset();
  moved_width = 320;
  CHECK(size.get() == 4 * 640 * 1080);
  size = 7;
  CHECK(size.get() == 7);

  KDBindings::Property<double> x{-2.5};
  KDBindings::Property<double> magnitude{0.0};
  magnitude = KDBindings::makeBinding(KDBindings::abs(x));
  CHECK(magnitude.get() == 2.5);
  x = 4.0;
  CHECK(magnitude.get() == 4.0);

  // A std::string starts initialized, so makeBoundProperty is safe here.
  KDBindings::Property<std::string> first{"Ada"};
  KDBindings::Property<std::string> name =
      KDBindings::makeBoundProperty(first + std::string(" Lovelace"));
  CHECK(name.get() == "Ada Lovelace");
  first = std::string("Grace");
  CHECK(name.get() == "Grace Lovelace");
}

void lazy_bindings() {
  KDBindings::BindingEvaluator evaluator;
  KDBindings::Property<int> a{-2};
  KDBindings::Property<int> b{3};
  KDBindings::Property<int> sum{0};
  sum = KDBindings::makeBinding(evaluator, a + b);
  KDBindings::Property<int> square_sum{0};
  square_sum = KDBindings::makeBinding(evaluator, paraboloid(a, b));
  CHECK(sum.get() == 1);
  CHECK(square_sum.get() == 13);

  a = 10;
  b = 20;
  CHECK(sum.get() == 1);
  CHECK(square_sum.get() == 13);

  evaluator.evaluateAll();
  CHECK(sum.get() == 30);
  CHECK(square_sum.get() == 500);
}

}  // namespace

int main() {
  signals();
  deferred();
  properties();
  immediate_bindings();
  lazy_bindings();
  if (failures != 0) {
    return 1;
  }
  std::printf("kdbindings 1.1.0: C++ API OK\n");
  return 0;
}
