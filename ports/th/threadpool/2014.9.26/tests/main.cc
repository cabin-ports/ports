#include <ThreadPool.h>

#include <cstdio>
#include <future>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

static int failures = 0;

#define CHECK(cond)                                                       \
  do {                                                                    \
    if (!(cond)) {                                                        \
      std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__,         \
                   __LINE__, #cond);                                      \
      ++failures;                                                         \
    }                                                                     \
  } while (0)

int main() {
  {
    ThreadPool pool(4);
    std::vector<std::future<int>> results;
    for (int i = 0; i < 8; ++i)
      results.emplace_back(pool.enqueue([](int x) { return x * x; }, i));
    int sum = 0;
    for (auto& r : results) sum += r.get();
    CHECK(sum == 140);

    CHECK(pool.enqueue([](int a, int b) { return a + b; }, 2, 3).get() == 5);

    auto failing = pool.enqueue([]() -> int { throw std::runtime_error("boom"); });
    try {
      failing.get();
      CHECK(false);
    } catch (const std::runtime_error& e) {
      CHECK(std::string(e.what()) == "boom");
    }
  }

  {
    std::mutex m;
    std::vector<int> order;
    {
      ThreadPool single(1);
      for (int i = 0; i < 10; ++i)
        single.enqueue([&m, &order, i] {
          std::lock_guard<std::mutex> lock(m);
          order.push_back(i);
        });
    }  // the destructor drains the queue and joins
    std::vector<int> expected;
    for (int i = 0; i < 10; ++i) expected.push_back(i);
    CHECK(order == expected);
  }

  return failures == 0 ? 0 : 1;
}
