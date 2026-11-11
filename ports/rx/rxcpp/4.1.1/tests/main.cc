#include <rxcpp/rx.hpp>
#include <rxcpp/rx-test.hpp>

#include <chrono>
#include <cstdio>
#include <exception>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

int fail(const char* what) {
  std::fprintf(stderr, "rxcpp: %s\n", what);
  return 1;
}

}  // namespace

int main() {
  // A cold source through filter and map delivers synchronously, in order.
  {
    std::vector<int> seen;
    bool completed = false;
    rxcpp::observable<>::range(1, 10)
        .filter([](int v) { return v % 2 == 0; })
        .map([](int v) { return v * v; })
        .subscribe([&](int v) { seen.push_back(v); },
                   [&]() { completed = true; });
    if (seen != std::vector<int>{4, 16, 36, 64, 100} || !completed)
      return fail("filter/map");
  }

  // reduce folds the whole sequence into one value.
  {
    int product = rxcpp::observable<>::range(1, 6)
                      .reduce(1, [](int acc, int v) { return acc * v; })
                      .as_blocking()
                      .last();
    if (product != 720) return fail("reduce");
  }

  // An error ends the stream: on_error sees the exception, and a later
  // on_next is dropped.
  {
    std::vector<int> seen;
    std::string message;
    bool completed = false;
    rxcpp::observable<>::create<int>([](rxcpp::subscriber<int> s) {
      s.on_next(1);
      s.on_error(std::make_exception_ptr(std::runtime_error("boom")));
      s.on_next(2);
    }).subscribe(
        [&](int v) { seen.push_back(v); },
        [&](std::exception_ptr ep) {
          try {
            std::rethrow_exception(ep);
          } catch (const std::runtime_error& e) {
            message = e.what();
          }
        },
        [&]() { completed = true; });
    if (seen != std::vector<int>{1} || message != "boom" || completed)
      return fail("on_error");
  }

  // A subject multicasts to its current subscribers only.
  {
    rxcpp::subjects::subject<std::string> subject;
    std::vector<std::string> early;
    std::vector<std::string> late;
    auto out = subject.get_subscriber();
    subject.get_observable().subscribe(
        [&](const std::string& v) { early.push_back(v); });
    out.on_next("a");
    subject.get_observable().subscribe(
        [&](const std::string& v) { late.push_back(v); });
    out.on_next("b");
    out.on_completed();
    if (early != std::vector<std::string>{"a", "b"} ||
        late != std::vector<std::string>{"b"})
      return fail("subject");
  }

  // subscribe_on moves the work to a new thread; as_blocking waits for it.
  {
    std::thread::id worker;
    int sum = rxcpp::observable<>::range(1, 100)
                  .map([&](int v) {
                    worker = std::this_thread::get_id();
                    return v;
                  })
                  .subscribe_on(rxcpp::observe_on_new_thread())
                  .as_blocking()
                  .sum();
    if (sum != 5050) return fail("subscribe_on sum");
    if (worker == std::thread::id() || worker == std::this_thread::get_id())
      return fail("subscribe_on thread");
  }

  // Virtual time: debounce on the test scheduler drops the value that a
  // newer one replaces within 10 ms and delays the rest by 10 ms.  Each
  // notification reaches it one tick late: the test scheduler moves work
  // scheduled for the current time to the next tick.
  {
    auto sc = rxcpp::schedulers::make_test();
    auto w = sc.create_worker();
    auto so = rxcpp::identity_one_worker(sc);
    const rxcpp::schedulers::test::messages<int> on;
    auto xs = sc.make_hot_observable({on.next(150, 0), on.next(210, 1),
                                      on.next(215, 2), on.next(240, 3),
                                      on.completed(300)});
    auto res = w.start([&]() {
      return xs.debounce(std::chrono::milliseconds(10), so);
    });
    auto expected = rxcpp::util::to_vector(
        {on.next(226, 2), on.next(251, 3), on.completed(301)});
    if (res.get_observer().messages() != expected) return fail("debounce");
    if (xs.subscriptions() != rxcpp::util::to_vector({on.subscribe(200, 300)}))
      return fail("debounce subscription");
  }
  return 0;
}
