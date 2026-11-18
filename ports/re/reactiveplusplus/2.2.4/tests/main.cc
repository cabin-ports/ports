#include <rpp/rpp.hpp>

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <functional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#define CHECK(cond)                                                    \
  do {                                                                 \
    if (!(cond)) {                                                     \
      std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #cond); \
      std::exit(1);                                                    \
    }                                                                  \
  } while (0)

namespace ops = rpp::operators;

static void test_pipeline() {
  std::vector<int> out;
  bool completed = false;
  rpp::source::just(1, 2, 3, 4, 5)
      | ops::filter([](int v) { return v % 2 == 1; })
      | ops::map([](int v) { return v * 10; })
      | ops::subscribe([&](int v) { out.push_back(v); }, [&] { completed = true; });
  CHECK((out == std::vector<int>{10, 30, 50}));
  CHECK(completed);

  // A seeded scan emits the seed first.
  std::vector<int> sums;
  rpp::source::just(1, 2, 3) | ops::scan(10, std::plus<int>{}) | ops::subscribe([&](int v) { sums.push_back(v); });
  CHECK((sums == std::vector<int>{10, 11, 13, 16}));

  std::string joined;
  rpp::source::just(1, 2, 3)
      | ops::reduce(std::string{}, [](std::string&& seed, int v) { return std::move(seed) + std::to_string(v) + ","; })
      | ops::subscribe([&](const std::string& v) { joined = v; });
  CHECK(joined == "1,2,3,");

  std::vector<std::vector<int>> buffers;
  rpp::source::from_iterable(std::vector<int>{1, 2, 3, 4, 5}) | ops::buffer(2)
      | ops::subscribe([&](const std::vector<int>& v) { buffers.push_back(v); });
  CHECK((buffers == std::vector<std::vector<int>>{{1, 2}, {3, 4}, {5}}));

  std::vector<int> distinct;
  rpp::source::just(1, 1, 2, 2, 3, 2, 1) | ops::distinct_until_changed()
      | ops::subscribe([&](int v) { distinct.push_back(v); });
  CHECK((distinct == std::vector<int>{1, 2, 3, 2, 1}));
}

static void test_errors() {
  // retry(2) resubscribes twice, then passes the error on.
  std::vector<int> seen;
  bool failed = false;
  rpp::source::concat(rpp::source::just(1, 2, 3),
                      rpp::source::error<int>(std::make_exception_ptr(std::runtime_error{"boom"})))
      | ops::retry(2)
      | ops::subscribe([&](int v) { seen.push_back(v); },
                       [&](const std::exception_ptr& e) {
                         try {
                           std::rethrow_exception(e);
                         } catch (const std::runtime_error& ex) {
                           failed = std::string{ex.what()} == "boom";
                         }
                       });
  CHECK((seen == std::vector<int>{1, 2, 3, 1, 2, 3, 1, 2, 3}));
  CHECK(failed);

  // An infinite retry is cut short by take.
  std::vector<int> taken;
  bool completed = false;
  rpp::source::concat(rpp::source::just(1, 2, 3), rpp::source::error<int>({}))
      | ops::retry()
      | ops::take(4)
      | ops::subscribe([&](int v) { taken.push_back(v); }, [&] { completed = true; });
  CHECK((taken == std::vector<int>{1, 2, 3, 1}));
  CHECK(completed);
}

static void test_subject() {
  rpp::subjects::publish_subject<int> subject;
  std::vector<int> first, second;
  subject.get_observable() | ops::subscribe([&](int v) { first.push_back(v); });
  auto observer = subject.get_observer();
  observer.on_next(1);
  subject.get_observable() | ops::map([](int v) { return -v; })
      | ops::subscribe([&](int v) { second.push_back(v); });
  observer.on_next(2);
  observer.on_completed();
  observer.on_next(3);
  CHECK((first == std::vector<int>{1, 2}));
  CHECK((second == std::vector<int>{-2}));

  // Disposing the subscription stops delivery.
  std::vector<int> got;
  rpp::subjects::publish_subject<int> source;
  auto d = source.get_observable() | ops::subscribe_with_disposable([&](int v) { got.push_back(v); });
  source.get_observer().on_next(1);
  d.dispose();
  source.get_observer().on_next(2);
  CHECK(d.is_disposed());
  CHECK((got == std::vector<int>{1}));
}

static void test_threads() {
  int sum = 0;
  std::thread::id emitter;
  rpp::source::just(1, 2, 3)
      | ops::subscribe_on(rpp::schedulers::new_thread{})
      | ops::as_blocking()
      | ops::subscribe([&](int v) {
          sum += v;
          emitter = std::this_thread::get_id();
        });
  CHECK(sum == 6);
  CHECK(emitter != std::this_thread::get_id());
}

int main() {
  test_pipeline();
  test_errors();
  test_subject();
  test_threads();
  std::puts("reactiveplusplus 2.2.4 ok");
  return 0;
}
