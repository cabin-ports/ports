#include <version>  // before c9y.h: libstdc++ defines __cpp_lib_coroutine only here

#include <c9y/c9y.h>

#include <atomic>
#include <cstdio>
#include <numeric>
#include <thread>
#include <vector>

static std::future<int> co_forty_two() { co_return 42; }

static int failures = 0;

#define CHECK(cond)                                                      \
  do {                                                                   \
    if (!(cond)) {                                                       \
      std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
      ++failures;                                                        \
    }                                                                    \
  } while (0)

int main() {
  using namespace std::chrono_literals;

  // queue: FIFO order, empty pop, pop_wait on a stopped, empty queue returns empty.
  c9y::queue<int> q;
  CHECK(!q.pop().has_value());
  q.push(1);
  q.emplace(2);
  q.push(3);
  CHECK(q.pop().value() == 1);
  CHECK(q.pop_wait().value() == 2);
  CHECK(q.pop_wait_for(10ms).value() == 3);
  CHECK(!q.pop_wait_for(10ms).has_value());
  q.stop();
  CHECK(!q.pop_wait().has_value());

  // thread_pool: every thread runs the function once.
  std::atomic<int> ran{0};
  c9y::thread_pool threads([&ran] { ++ran; }, 4);
  CHECK(threads.get_concurency() == 4);
  threads.join();
  CHECK(ran == 4);

  // task_pool: every queued task runs.  Waits on its own counter, not flush(),
  // whose upstream lost-wakeup race could hang the test.
  std::atomic<int> sum{0};
  std::atomic<int> done{0};
  {
    c9y::task_pool tasks(3);
    for (int i = 1; i <= 100; ++i) {
      tasks.enqueue([&sum, &done, i] {
        sum += i;
        ++done;
      });
    }
    for (int n = 0; n < 3000 && done < 100; ++n) std::this_thread::sleep_for(10ms);
    CHECK(done == 100);
    CHECK(sum == 5050);
  }

  // async: the future carries the function's result.
  auto answer = c9y::async<int>([] { return 6 * 7; });
  CHECK(answer.get() == 42);

  // latch: released once every count_down arrived.
  c9y::latch latch(3);
  std::atomic<int> arrived{0};
  c9y::thread_pool counters(
      [&] {
        ++arrived;
        latch.count_down();
      },
      3);
  latch.wait();
  CHECK(arrived == 3);
  counters.join();

  // parallel algorithms over several chunks.
  std::vector<int> values(1000);
  std::iota(values.begin(), values.end(), 1);
  CHECK(c9y::parallel_reduce(values.begin(), values.end(), 0, 100u) == 500500);
  CHECK(c9y::parallel_count(values.begin(), values.end(), 7, 100u) == 1);
  std::vector<int> doubled(values.size());
  c9y::parallel_transform(values.begin(), values.end(), doubled.begin(),
                          [](int v) { return v * 2; }, 100u);
  CHECK(doubled.front() == 2 && doubled.back() == 2000);
  std::atomic<long> total{0};
  c9y::parallel_for_each(values.begin(), values.end(),
                         [&total](int v) { total += v; }, 100u);
  CHECK(total == 500500);

  CHECK(co_forty_two().get() == 42);

  if (failures == 0) std::puts("ok");
  return failures == 0 ? 0 : 1;
}
