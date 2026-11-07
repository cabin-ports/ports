// Consumer test for cabin-ports/ltla-subpar: parallelize_range() and
// parallelize_simple() through the default std::thread scheme (worker 0
// on the calling thread, the others on their own threads, all running at
// once), their partitioning and worker counts, exceptions thrown on the
// calling thread and in worker threads, the nothrow variants, and
// sanitize_num_workers() with several integer types.
#include <subpar/subpar.hpp>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char* what) {
  if (!ok) {
    std::printf("FAILED: %s\n", what);
    ++failures;
  }
}

// Each caller waits here until n callers have arrived, which only happens
// if they run at the same time; false if the deadline passes first.
bool meet(std::atomic<int>& arrived, int n) {
  ++arrived;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  while (arrived.load() < n && std::chrono::steady_clock::now() < deadline)
    std::this_thread::yield();
  return arrived.load() == n;
}

std::string range_error(int thrower) {
  try {
    subpar::parallelize_range(3, 30, [thrower](int w, int, int) {
      if (w == thrower)
        throw std::runtime_error("worker " + std::to_string(w));
    });
  } catch (std::runtime_error const& e) {
    return e.what();
  }
  return "";
}

std::string simple_error(int thrower) {
  try {
    subpar::parallelize_simple(4, [thrower](int w) {
      if (w == thrower)
        throw std::out_of_range("task " + std::to_string(w));
    });
  } catch (std::out_of_range const& e) {
    return e.what();
  }
  return "";
}

}  // namespace

int main() {
  const std::thread::id main_id = std::this_thread::get_id();

  // 1003 tasks over 4 workers: 250 each, and the first three take one of
  // the 3 left over.
  const int num_tasks = 1003;
  std::vector<int> owner(num_tasks, -1);
  std::vector<int> calls(4, 0), starts(4, -1), lengths(4, 0);
  std::vector<std::thread::id> ids(4);
  std::vector<long long> partial(4, 0);
  int used = subpar::parallelize_range(4, num_tasks, [&](int w, int start, int length) {
    ++calls[w];
    starts[w] = start;
    lengths[w] = length;
    ids[w] = std::this_thread::get_id();
    for (int t = start; t < start + length; ++t) {
      owner[t] = w;
      partial[w] += static_cast<long long>(t) * t;
    }
  });
  check(used == 4, "four workers are used");
  check(calls == std::vector<int>(4, 1), "each worker runs once");
  check(starts == std::vector<int>{0, 251, 502, 753} &&
            lengths == std::vector<int>{251, 251, 251, 250},
        "the first workers take the remainder");
  bool covered = true;
  for (int t = 0; t < num_tasks; ++t)
    covered = covered && owner[t] == (t < 753 ? t / 251 : 3);
  check(covered, "the ranges cover every task once, in order");
  check(partial[0] + partial[1] + partial[2] + partial[3] == 335839505LL,
        "the workers' partial sums add up");
  check(ids[0] == main_id, "worker 0 runs on the calling thread");
  check(ids[1] != main_id && ids[2] != main_id && ids[3] != main_id,
        "the other workers run on their own threads");

  // All four workers, or tasks, must be running at once.
  std::atomic<int> arrived{0};
  std::vector<int> met(4, 0);
  subpar::parallelize_range(4, 4, [&](int w, int, int) { met[w] = meet(arrived, 4); });
  check(met == std::vector<int>(4, 1), "the workers run concurrently");
  arrived = 0;
  met.assign(4, 0);
  subpar::parallelize_range<true>(4, 4, [&](int w, int, int) { met[w] = meet(arrived, 4); });
  check(met == std::vector<int>(4, 1), "the nothrow workers run concurrently");
  arrived = 0;
  met.assign(4, 0);
  subpar::parallelize_simple(4, [&](int w) { met[w] = meet(arrived, 4); });
  check(met == std::vector<int>(4, 1), "parallelize_simple runs its tasks concurrently");
  arrived = 0;
  met.assign(4, 0);
  subpar::parallelize_simple<true>(4, [&](int w) { met[w] = meet(arrived, 4); });
  check(met == std::vector<int>(4, 1), "the nothrow parallelize_simple runs concurrently");

  check(range_error(0) == "worker 0", "an exception on the calling thread reaches the caller");
  check(range_error(2) == "worker 2", "an exception in a worker thread reaches the caller");

  // More workers than tasks: one task each.  No workers, or no tasks.
  std::vector<int> single(10, 0);
  int used_nothrow = subpar::parallelize_range<true>(16, 10, [&](int w, int start, int length) {
    single[w] = start == w && length == 1;
  });
  check(used_nothrow == 10 && single == std::vector<int>(10, 1),
        "extra workers are not used");
  int whole = 0;
  check(subpar::parallelize_range(0, 5, [&](int w, int start, int length) {
          whole = w == 0 && start == 0 && length == 5;
        }) == 1 &&
            whole == 1,
        "zero workers run everything on one");
  int empty_calls = 0;
  check(subpar::parallelize_range(4, 0, [&](int, int, int) { ++empty_calls; }) == 0 &&
            empty_calls == 0,
        "no tasks, no calls");

  std::vector<std::size_t> wide(3, 0);
  int used_wide = subpar::parallelize_range(
      3, std::size_t{7}, [&](int w, std::size_t, std::size_t length) { wide[w] = length; });
  check(used_wide == 3 && wide == std::vector<std::size_t>{3, 2, 2},
        "a std::size_t task count");

  std::vector<int> ran(5, 0);
  std::vector<std::thread::id> simple_ids(5);
  subpar::parallelize_simple(5, [&](int w) {
    ++ran[w];
    simple_ids[w] = std::this_thread::get_id();
  });
  bool own_threads = simple_ids[0] == main_id;
  for (int w = 1; w < 5; ++w)
    own_threads = own_threads && simple_ids[w] != main_id;
  check(ran == std::vector<int>(5, 1), "parallelize_simple runs each task once");
  check(own_threads, "parallelize_simple runs tasks 1-4 on their own threads");

  check(simple_error(0) == "task 0" && simple_error(3) == "task 3",
        "parallelize_simple rethrows a task's exception from any thread");

  std::vector<int> ran_nothrow(3, 0);
  subpar::parallelize_simple<true>(3, [&](int w) { ran_nothrow[w] = w + 1; });
  check(ran_nothrow == std::vector<int>{1, 2, 3}, "the nothrow parallelize_simple");

  check(subpar::sanitize_num_workers(10, 20) == 10 && subpar::sanitize_num_workers(10, 5) == 5,
        "sanitize_num_workers caps the workers at the tasks");
  check(subpar::sanitize_num_workers(-1, 5) == 1 && subpar::sanitize_num_workers(0, 0) == 0,
        "sanitize_num_workers for no workers");
  check(subpar::sanitize_num_workers(10, std::size_t{5}) == 5 &&
            subpar::sanitize_num_workers(10, static_cast<std::size_t>(-1)) == 10 &&
            subpar::sanitize_num_workers(1000, std::uint8_t{255}) == 255,
        "sanitize_num_workers across integer types");

  std::printf("ltla-subpar (C++): %s\n", failures == 0 ? "ok" : "FAILED");
  return failures == 0 ? 0 : 1;
}
