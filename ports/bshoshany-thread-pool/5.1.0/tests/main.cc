// Consumer test: submit_task's future carries a result and a thrown
// exception; detached tasks all run before wait() returns; submit_loop,
// submit_blocks and submit_sequence cover an index range exactly once;
// BS::this_thread names the worker and its pool; reset resizes the pool; a
// paused pool queues tasks until unpaused and purge drops them; a priority
// pool runs higher priorities first; wait() from a worker of a deadlock-
// checking pool throws; synced_stream keeps lines whole; and the version.
#include <BS_thread_pool.hpp>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <future>
#include <numeric>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char* what, int line) {
  if (!ok) {
    std::fprintf(stderr, "main.cc:%d: check failed: %s\n", line, what);
    ++failures;
  }
}
#define CHECK(cond) check((cond), #cond, __LINE__)

}  // namespace

int main() {
  CHECK(BS::thread_pool_version == BS::version(5, 1, 0));
  CHECK(!BS::thread_pool_module && !BS::thread_pool_native_extensions);

  BS::thread_pool<> pool(4);
  CHECK(pool.get_thread_count() == 4);

  // submit_task returns a future for the task's result or exception.
  std::future<int> answer = pool.submit_task([] { return 6 * 7; });
  CHECK(answer.get() == 42);
  std::future<void> failing = pool.submit_task([] { throw std::runtime_error("task failed"); });
  bool caught = false;
  try {
    failing.get();
  } catch (const std::runtime_error& e) {
    caught = std::string(e.what()) == "task failed";
  }
  CHECK(caught);

  // wait() returns once every detached task has run.
  std::atomic<int> counter{0};
  for (int i = 0; i < 100; ++i) pool.detach_task([&counter] { ++counter; });
  pool.wait();
  CHECK(counter == 100 && pool.get_tasks_total() == 0);

  // submit_loop runs the body once per index, split into the given blocks.
  std::vector<int> hits(1000, 0);
  BS::multi_future<void> loop = pool.submit_loop(std::size_t{0}, hits.size(), [&hits](std::size_t i) { ++hits[i]; }, 7);
  CHECK(loop.size() == 7);
  loop.get();
  CHECK(std::count(hits.begin(), hits.end(), 1) == 1000);

  // submit_blocks hands each block its range; the partial sums add up.
  BS::multi_future<long long> sums = pool.submit_blocks(
      1, 1001,
      [](int start, int end) {
        long long sum = 0;
        for (int i = start; i < end; ++i) sum += i;
        return sum;
      },
      5);
  std::vector<long long> parts = sums.get();
  CHECK(parts.size() == 5 && std::accumulate(parts.begin(), parts.end(), 0LL) == 500500);

  // submit_sequence runs one task per index; get() keeps index order.
  std::vector<int> tens = pool.submit_sequence(0, 10, [](int i) { return i * 10; }).get();
  CHECK(tens.size() == 10 && tens[3] == 30 && tens[9] == 90);

  // BS::this_thread knows a worker's index and pool, and nothing outside one.
  CHECK(!BS::this_thread::get_index().has_value() && !BS::this_thread::get_pool().has_value());
  std::optional<std::size_t> index = pool.submit_task([] { return BS::this_thread::get_index(); }).get();
  CHECK(index.has_value() && *index < 4);
  void* const self = &pool;
  CHECK(pool.submit_task([self] { return BS::this_thread::get_pool() == std::optional<void*>(self); }).get());

  // reset replaces the workers with a new number of threads.
  pool.reset(2);
  CHECK(pool.get_thread_count() == 2 && pool.get_thread_ids().size() == 2);

  // A paused pool queues tasks without running them; purge drops them.
  {
    BS::pause_thread_pool paused(2);
    paused.pause();
    CHECK(paused.is_paused());
    std::atomic<int> ran{0};
    for (int i = 0; i < 5; ++i) paused.detach_task([&ran] { ++ran; });
    CHECK(paused.get_tasks_queued() == 5 && ran == 0);
    paused.purge();
    CHECK(paused.get_tasks_queued() == 0);
    paused.unpause();
    for (int i = 0; i < 3; ++i) paused.detach_task([&ran] { ++ran; });
    paused.wait();
    CHECK(!paused.is_paused() && ran == 3);
  }

  // One worker runs queued tasks from the highest priority down.
  {
    BS::thread_pool<BS::tp::priority | BS::tp::pause> ordered(1);
    ordered.pause();
    std::string order;
    ordered.detach_task([&order] { order += 'l'; }, BS::pr::low);
    ordered.detach_task([&order] { order += 'H'; }, BS::pr::highest);
    ordered.detach_task([&order] { order += 'n'; }, BS::pr::normal);
    ordered.unpause();
    ordered.wait();
    CHECK(order == "Hnl");
  }

  // Waiting on a pool from one of its own workers would deadlock: it throws.
  {
    BS::wdc_thread_pool checked(1);
    std::future<bool> deadlock = checked.submit_task([&checked] {
      try {
        checked.wait();
      } catch (const BS::wait_deadlock&) {
        return true;
      }
      return false;
    });
    CHECK(deadlock.get());
  }

  // synced_stream writes each println as one whole line.
  {
    std::ostringstream out;
    BS::synced_stream sync(out);
    pool.submit_sequence(0, 4, [&sync](int i) { sync.println("line ", i); }).get();
    const std::string text = out.str();
    bool whole = true;
    for (int i = 0; i < 4; ++i) whole = whole && text.find("line " + std::to_string(i) + "\n") != std::string::npos;
    CHECK(whole && text.size() == 4 * std::string("line 0\n").size());
  }

  if (failures != 0) return 1;
  std::puts("bshoshany-thread-pool: ok");
  return 0;
}
