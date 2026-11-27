// First, to check that task_canceled.h stands alone.
#include <async/task_canceled.h>

#include <async/awaitable_get.h>
#include <async/awaitable_then.h>
#include <async/event_signal.h>
#include <async/task.h>
#include <async/task_completion_source.h>
#include <async/to_future.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <future>
#include <stdexcept>
#include <string>

namespace {

void check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    std::exit(1);
  }
}

async::task<int> answer() { co_return 42; }

async::task<int> add(int a, int b) {
  int x = co_await answer();
  co_return x + a + b;
}

async::task<void> nothing() { co_return; }

async::task<int> fails() {
  throw std::logic_error{"boom"};
  co_return 0;
}

async::task<std::string> wait_for(async::task_completion_source<int>& src) {
  int v = co_await src.task();
  co_return "got " + std::to_string(v);
}

template <typename F>
std::string what_of(F&& f) {
  try {
    f();
  } catch (const std::exception& e) {
    return e.what();
  }
  return "no exception";
}

void test_task_and_awaitable_get() {
  check(async::awaitable_get(answer()) == 42, "awaitable_get of co_return");
  check(async::awaitable_get(add(1, 2)) == 45, "co_await through nested task");
  async::awaitable_get(nothing());
  check(what_of([] { async::awaitable_get(fails()); }) == "boom",
        "awaitable_get rethrows the coroutine's exception");

  async::task<int> once = answer();
  check(async::awaitable_get(std::move(once)) == 42, "first await");
  async::task<int> twice = answer();
  async::awaitable_get(twice);
  check(what_of([&] { async::awaitable_get(twice); }) ==
            "task<T> may be co_awaited (or have await_resume() used) only once.",
        "second await is refused");
}

void test_to_future() {
  std::future<int> f = async::to_future(add(10, 20));
  check(f.get() == 72, "to_future value");
  std::future<void> v = async::to_future(nothing());
  v.get();
  std::future<int> e = async::to_future(fails());
  check(what_of([&] { (void)e.get(); }) == "boom", "to_future exception");
}

void test_completion_source() {
  async::task_completion_source<int> ready;
  ready.set_value(7);
  check(async::awaitable_get(ready.task()) == 7, "set_value before await");
  check(what_of([&] { ready.set_value(8); }) ==
            "The task_completion_source<T> has already been completed.",
        "double set_value is refused");
  check(!ready.try_set_value(9), "try_set_value after completion");

  // A coroutine suspended on the source resumes inside set_value.
  async::task_completion_source<int> src;
  async::task<std::string> waiter = wait_for(src);
  check(!waiter.await_ready(), "waiter is suspended");
  src.set_value(5);
  check(async::awaitable_get(std::move(waiter)) == "got 5",
        "set_value resumes the waiter");

  async::task_completion_source<int> bad;
  async::task<std::string> pending = wait_for(bad);
  bad.set_exception(std::make_exception_ptr(std::runtime_error{"failed"}));
  check(what_of([&] { async::awaitable_get(std::move(pending)); }) == "failed",
        "set_exception reaches the waiter");
  check(what_of([&] { bad.set_exception(nullptr); }) ==
            "The exception_ptr must not be empty.",
        "empty exception_ptr is refused");

  async::task_completion_source<void> done;
  done.set_value();
  async::awaitable_get(done.task());
}

void test_then_and_signal() {
  async::event_signal signal;
  int seen = 0;
  async::awaitable_then(answer(), [&](async::awaitable_result<int> r) {
    seen = r();
    signal.set();
  });
  signal.wait();
  check(seen == 42, "awaitable_then result");

  std::string error;
  async::event_signal failed;
  async::awaitable_then(fails(), [&](async::awaitable_result<int> r) {
    error = what_of([&] { r(); });
    failed.set();
  });
  failed.wait();
  check(error == "boom", "awaitable_then exception");

  bool voidDone = false;
  async::awaitable_then(nothing(), [&](async::awaitable_result<void> r) {
    r();
    voidDone = true;
  });
  check(voidDone, "awaitable_then void");

  async::event_signal s;
  check(!s.is_set(), "signal starts clear");
  check(!s.wait_for(std::chrono::milliseconds{0}), "wait_for times out");
  check(what_of([&] { s.wait_for_or_throw(std::chrono::milliseconds{0}); }) ==
            "Wait timed out.",
        "wait_for_or_throw times out");
  s.set();
  check(s.is_set() && s.wait_for(std::chrono::milliseconds{0}), "signal set");
}

void test_task_canceled() {
  check(what_of([] { throw async::task_canceled{}; }) == "task canceled",
        "task_canceled message");
}

}  // namespace

int main() {
  test_task_and_awaitable_get();
  test_to_future();
  test_completion_source();
  test_then_and_signal();
  test_task_canceled();
  std::puts("ok");
  return 0;
}
