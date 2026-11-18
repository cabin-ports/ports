#include <taskflow/taskflow.hpp>
// The algorithms' definitions are in their own headers.
#include <taskflow/algorithm/for_each.hpp>
#include <taskflow/algorithm/reduce.hpp>
#include <taskflow/algorithm/scan.hpp>
#include <taskflow/algorithm/sort.hpp>
#include <taskflow/utility/object_pool.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <mutex>
#include <numeric>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

int failures = 0;

#define CHECK(cond)                                                            \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__,    \
                   #cond);                                                     \
      ++failures;                                                              \
    }                                                                          \
  } while (0)

// A task graph A -> {B, C} -> D runs A first and D last.
void check_graph(tf::Executor &executor) {
  std::mutex mutex;
  std::string order;
  auto record = [&](char c) {
    return [&, c] {
      std::lock_guard<std::mutex> lock(mutex);
      order += c;
    };
  };
  tf::Taskflow taskflow;
  auto [a, b, c, d] = taskflow.emplace(record('A'), record('B'), record('C'),
                                       record('D'));
  a.precede(b, c);
  d.succeed(b, c);
  executor.run(taskflow).wait();
  CHECK(order.size() == 4 && order.front() == 'A' && order.back() == 'D');
  CHECK(order == "ABCD" || order == "ACBD");

  // Running the same graph three more times repeats it in full.
  order.clear();
  executor.run_n(taskflow, 3).wait();
  CHECK(order.size() == 12 && std::count(order.begin(), order.end(), 'D') == 3);
}

// A condition task returns the index of the successor to run: 0 loops back,
// 1 leaves the loop.
void check_condition(tf::Executor &executor) {
  int counter = 0;
  bool done = false;
  tf::Taskflow taskflow;
  auto init = taskflow.emplace([&] { counter = 0; });
  auto body = taskflow.emplace([&] { ++counter; });
  auto cond = taskflow.emplace([&] { return counter < 10 ? 0 : 1; });
  auto stop = taskflow.emplace([&] { done = true; });
  init.precede(body);
  body.precede(cond);
  cond.precede(body, stop);
  executor.run(taskflow).wait();
  CHECK(counter == 10 && done);
}

// A subflow spawns tasks at run time and joins them before its successor.
void check_subflow(tf::Executor &executor) {
  std::atomic<int> sum{0};
  int seen = -1;
  tf::Taskflow taskflow;
  auto spawn = taskflow.emplace([&](tf::Subflow &sf) {
    for (int i = 1; i <= 8; ++i)
      sf.emplace([&sum, i] {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        sum += i;
      });
  });
  auto after = taskflow.emplace([&] { seen = sum.load(); });
  spawn.precede(after);
  executor.run(taskflow).wait();
  CHECK(seen == 36);
}

void check_algorithms(tf::Executor &executor) {
  std::vector<int> v(1000);
  std::iota(v.begin(), v.end(), 1);
  int sum = 100;
  std::vector<std::int64_t> squares(1000, -1), prefix(1000), expect(1000);
  std::inclusive_scan(v.begin(), v.end(), expect.begin());
  tf::Taskflow taskflow;
  taskflow.reduce(v.begin(), v.end(), sum, std::plus<int>{});
  taskflow.for_each_index(0, 1000, 1, [&](int i) {
    squares[static_cast<std::size_t>(i)] = std::int64_t{i} * i;
  });
  taskflow.inclusive_scan(v.begin(), v.end(), prefix.begin(),
                          std::plus<std::int64_t>{});
  executor.run(taskflow).wait();
  CHECK(sum == 500600);
  CHECK(squares[0] == 0 && squares[999] == 998001);
  CHECK(std::accumulate(squares.begin(), squares.end(), std::int64_t{0}) ==
        332833500);
  CHECK(prefix[0] == 1 && prefix[9] == 55 && prefix[999] == 500500);
  CHECK(prefix == expect);
}

// tf::sort is a parallel pattern-defeating quicksort; compare it with
// std::sort on inputs large enough to partition in parallel.
void check_sort(tf::Executor &executor) {
  std::uint32_t state = 12345;
  std::vector<int> random(200000), sorted_desc(50000);
  for (auto &x : random) {
    state = state * 1103515245u + 12345u;
    x = static_cast<int>((state >> 8) % 100000u) - 50000;
  }
  std::iota(sorted_desc.rbegin(), sorted_desc.rend(), 0);
  std::vector<int> expect = random;
  std::sort(expect.begin(), expect.end());
  std::vector<int> small{5, 3, 9, 1, 7, 3};
  // Few distinct keys sorted with a lambda (not std::less/greater) take the
  // generic and equal-key partitions; a nearly ascending run takes the
  // insertion sorts.
  std::vector<int> dups(100000), nearly(100000);
  for (auto &x : dups) {
    state = state * 1103515245u + 12345u;
    x = static_cast<int>((state >> 8) % 4u);
  }
  std::iota(nearly.begin(), nearly.end(), 0);
  for (std::size_t i = 0; i + 1 < nearly.size(); i += 64)
    std::swap(nearly[i], nearly[i + 1]);
  std::vector<int> dups_expect = dups, ascending(nearly.size());
  std::iota(ascending.begin(), ascending.end(), 0);
  std::sort(dups_expect.begin(), dups_expect.end());

  tf::Taskflow taskflow;
  taskflow.sort(random.begin(), random.end());
  taskflow.sort(sorted_desc.begin(), sorted_desc.end());
  taskflow.sort(small.begin(), small.end(), std::greater<int>{});
  taskflow.sort(dups.begin(), dups.end(), [](int a, int b) { return a < b; });
  taskflow.sort(nearly.begin(), nearly.end());
  executor.run(taskflow).wait();
  CHECK(random == expect);
  CHECK(sorted_desc.front() == 0 && sorted_desc.back() == 49999 &&
        std::is_sorted(sorted_desc.begin(), sorted_desc.end()));
  CHECK((small == std::vector<int>{9, 7, 5, 3, 3, 1}));
  CHECK(dups == dups_expect);
  CHECK(nearly == ascending);
}

void check_async(tf::Executor &executor) {
  auto future = executor.async([] { return 6 * 7; });
  CHECK(future.get() == 42);

  // B and C depend on A; D depends on both.
  std::atomic<int> value{1};
  auto a = executor.silent_dependent_async([&] { value = 2; });
  auto b = executor.silent_dependent_async([&] { value += 3; }, a);
  auto c = executor.silent_dependent_async([&] { value += 4; }, a);
  auto [d, fd] = executor.dependent_async([&] { return value * 10; }, b, c);
  CHECK(fd.get() == 90);
  executor.wait_for_all();
}

// The executor's default notifier, used directly: a waiter that commits
// sleeps until notified, and one that cancels never blocks.
void check_notifier() {
  tf::NonblockingNotifier notifier(2);
  std::atomic<bool> ready{false};
  std::atomic<bool> woke{false};
  std::thread waiter([&] {
    while (!ready.load()) {
      notifier.prepare_wait(0);
      if (ready.load()) {
        notifier.cancel_wait(0);
        break;
      }
      notifier.commit_wait(0);
    }
    woke = true;
  });
  // Notify only once the waiter has committed and parked.
  while (notifier.num_waiters() != 1)
    std::this_thread::yield();
  CHECK(!woke.load());
  ready = true;
  notifier.notify_one();
  waiter.join();
  CHECK(woke.load() && notifier.num_waiters() == 0);

  notifier.prepare_wait(1);
  notifier.cancel_wait(1);
  notifier.notify_all();
  // Tickets are served in order, so a cancel must advance the epoch or the
  // second cancel never returns.
  notifier.prepare_wait(0);
  notifier.prepare_wait(1);
  notifier.cancel_wait(0);
  notifier.cancel_wait(1);
  CHECK(notifier.size() == 2 && notifier.num_waiters() == 0);
}

int destroyed = 0;

struct Item {
  int value;
  std::string name;
  Item(int v, std::string n) : value(v), name(std::move(n)) {}
  ~Item() { ++destroyed; }
};

// tf::ObjectPool with its default 16-byte TaggedHead128 free-list head.  With
// two shards, each thread's allocations alternate between them, so the third
// and fourth allocations pop the blocks the first two recycled.
void check_object_pool() {
  tf::ObjectPool<Item, tf::TaggedHead128, 1> pool;
  Item *a = pool.animate(1, "one");
  Item *b = pool.animate(2, "two");
  CHECK(a != b && a->value == 1 && a->name == "one" && b->name == "two");
  pool.recycle(a);
  pool.recycle(b);
  CHECK(destroyed == 2);
  Item *c = pool.animate(3, "three");
  Item *d = pool.animate(4, "four");
  CHECK(c == a && d == b && c->name == "three" && d->value == 4);
  pool.recycle(c);
  pool.recycle(d);
  pool.recycle(nullptr);
  CHECK(destroyed == 4);
}

} // namespace

int main() {
  CHECK(TF_VERSION == 400100);
  tf::Executor executor(4);
  CHECK(executor.num_workers() == 4);

  check_graph(executor);
  check_condition(executor);
  check_subflow(executor);
  check_algorithms(executor);
  check_sort(executor);
  check_async(executor);
  check_notifier();
  check_object_pool();

  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("taskflow ok");
  return 0;
}
