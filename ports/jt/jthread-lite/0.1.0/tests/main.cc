#include <nonstd/jthread.hpp>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>
#include <utility>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

// Workers sleep first, so a result read right after a join or a scope's end
// is unlikely to come from a worker that merely won a race.
void sleep_briefly() {
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
}

void multiply(int* out, int x, int y) {
  sleep_briefly();
  *out = x * y;
}

}  // namespace

int main() {
  {
    nonstd::jthread idle;
    check(!idle.joinable(), "a default jthread is not joinable");
    check(idle.get_id() == nonstd::jthread::id(),
          "a default jthread has no id");
  }

  // The destructor joins, so the result is visible once the scope ends.
  int product = 0;
  {
    nonstd::jthread worker(multiply, &product, 6, 7);
    check(worker.joinable(), "a running jthread is joinable");
    check(worker.get_id() != std::this_thread::get_id(),
          "the worker has its own thread");
  }
  check(product == 42, "the destructor joined the worker");

  int sum = 0;
  nonstd::jthread adder(
      [&sum](int a, int b) {
        sleep_briefly();
        sum = a + b;
      },
      40, 2);
  adder.join();
  check(!adder.joinable(), "a joined jthread is not joinable");
  check(sum == 42, "join waited for the worker");

  std::atomic<int> total(0);
  {
    std::vector<nonstd::jthread> pool;
    for (int i = 0; i < 4; ++i) {
      pool.emplace_back([&total] {
        sleep_briefly();
        for (int n = 0; n < 1000; ++n) ++total;
      });
    }
  }
  check(total == 4000, "destroying the vector joined every worker");

  std::atomic<int> moved_runs(0);
  {
    nonstd::jthread first([&moved_runs] { ++moved_runs; });
    const nonstd::jthread::id id = first.get_id();
    nonstd::jthread second(std::move(first));
    check(!first.joinable(), "a moved-from jthread is not joinable");
    check(second.get_id() == id, "move construction keeps the thread");
  }
  check(moved_runs == 1, "the moved thread ran once and was joined");

  // Move assignment joins the target's thread first, where std::thread's
  // would call std::terminate.
  std::atomic<bool> slow_done(false);
  std::atomic<bool> fast_done(false);
  {
    nonstd::jthread target([&slow_done] {
      sleep_briefly();
      slow_done = true;
    });
    nonstd::jthread source([&fast_done] { fast_done = true; });
    const nonstd::jthread::id source_id = source.get_id();
    target = std::move(source);
    check(slow_done, "move assignment joined the replaced thread");
    check(target.get_id() == source_id, "move assignment took the thread");
    check(!source.joinable(), "the moved-from source is not joinable");
  }
  check(fast_done, "the assigned thread was joined");

  {
    nonstd::jthread a([] {});
    nonstd::jthread b([] {});
    const nonstd::jthread::id ida = a.get_id();
    const nonstd::jthread::id idb = b.get_id();
    a.swap(b);
    check(a.get_id() == idb && b.get_id() == ida, "member swap");
    swap(a, b);
    check(a.get_id() == ida && b.get_id() == idb, "free swap");
  }

  {
    nonstd::jthread detached([] {});
    detached.detach();
    check(!detached.joinable(), "a detached jthread is not joinable");
  }

  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("jthread-lite consumer test passed");
  return 0;
}
