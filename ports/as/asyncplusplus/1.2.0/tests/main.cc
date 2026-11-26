// Runs tasks on the default thread pool, chains continuations, joins and races
// tasks, runs the parallel algorithms, and checks exception and cancellation
// propagation.
#include <async++.h>

#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

static int failures = 0;

#define CHECK(cond)                                              \
  do {                                                           \
    if (!(cond)) {                                               \
      std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #cond); \
      ++failures;                                                \
    }                                                            \
  } while (0)

int main() {
  // spawn and a continuation chain
  auto chain = async::spawn([] { return 20; })
                   .then([](int v) { return v + 22; })
                   .then([](int v) { return std::to_string(v); });
  CHECK(chain.get() == "42");

  // when_all of a vector keeps the tasks in order; when_any reports the finished index
  std::vector<async::task<int>> tasks;
  for (int i = 0; i < 8; ++i)
    tasks.push_back(async::spawn([i] { return i * i; }));
  std::vector<async::task<int>> done = async::when_all(tasks).get();
  CHECK(done.size() == 8);
  for (int i = 0; i < static_cast<int>(done.size()); ++i)
    CHECK(done[i].get() == i * i);

  std::vector<async::task<int>> any_tasks;
  any_tasks.push_back(async::make_task(7));
  auto any = async::when_any(any_tasks).get();
  CHECK(any.index == 0 && any.tasks.size() == 1 && any.tasks[0].get() == 7);

  // parallel_for and parallel_reduce over 1..10000
  std::vector<int> data(10000);
  async::parallel_for(async::irange(0, 10000), [&data](int i) { data[i] = i + 1; });
  long long sum = async::parallel_reduce(
      data, 0LL, [](long long x, long long y) { return x + y; });
  CHECK(sum == 50005000LL);
  long long squares_sum = async::parallel_map_reduce(
      async::irange(1, 101), 0LL,
      [](int x) { return static_cast<long long>(x) * x; },
      [](long long x, long long y) { return x + y; });
  CHECK(squares_sum == 338350LL);

  // parallel_invoke runs every function
  int a = 0, b = 0, c = 0;
  async::parallel_invoke([&a] { a = 1; }, [&b] { b = 2; }, [&c] { c = 3; });
  CHECK(a == 1 && b == 2 && c == 3);

  // an exception thrown in a task reaches get()
  auto thrower = async::spawn([]() -> int { throw std::runtime_error("boom"); });
  bool caught = false;
  try {
    thrower.get();
  } catch (const std::runtime_error& e) {
    caught = std::string(e.what()) == "boom";
  }
  CHECK(caught);

  // an event_task completed by hand wakes a waiting continuation
  async::event_task<int> event;
  auto waiting = event.get_task().then([](int v) { return v * 2; });
  event.set(21);
  CHECK(waiting.get() == 42);

  // async::task_canceled reaches get() when the task is canceled
  async::cancellation_token token;
  token.cancel();
  auto canceled = async::spawn([&token] { async::interruption_point(token); return 1; });
  bool was_canceled = false;
  try {
    canceled.get();
  } catch (const async::task_canceled&) {
    was_canceled = true;
  }
  CHECK(was_canceled);

  // inline_scheduler runs the task on the caller's thread
  auto inline_task = async::spawn(async::inline_scheduler(), [] { return 5; });
  CHECK(inline_task.ready() && inline_task.get() == 5);

  if (failures == 0)
    std::puts("ok");
  return failures == 0 ? 0 : 1;
}
