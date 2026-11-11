// The port links no libaio (see cabin.toml), so build SimpleExecutor without
// it even where <libaio.h> exists.
#define ASYNC_SIMPLE_HAS_NOT_AIO

#include <async_simple/Collect.h>
#include <async_simple/Future.h>
#include <async_simple/Promise.h>
#include <async_simple/Signal.h>
#include <async_simple/Try.h>
#include <async_simple/coro/Collect.h>
#include <async_simple/coro/FutureAwaiter.h>
#include <async_simple/coro/Generator.h>
#include <async_simple/coro/Latch.h>
#include <async_simple/coro/Lazy.h>
#include <async_simple/coro/Mutex.h>
#include <async_simple/coro/Semaphore.h>
#include <async_simple/coro/SharedMutex.h>
#include <async_simple/coro/Sleep.h>
#include <async_simple/coro/SyncAwait.h>
#include <async_simple/executors/SimpleExecutor.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

using namespace async_simple::coro;
using async_simple::CurrentExecutor;
using async_simple::Executor;
using async_simple::Future;
using async_simple::Promise;
using async_simple::SignalType;
using async_simple::Try;
using async_simple::executors::SimpleExecutor;
using namespace std::chrono_literals;

namespace {

std::atomic<int> failures{0};

#define CHECK(cond)                                                   \
  do {                                                                \
    if (!(cond)) {                                                    \
      std::fprintf(stderr, "FAIL: %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      ++failures;                                                     \
    }                                                                 \
  } while (0)

// Waits up to ten seconds for a flag another thread sets.  A task that never
// sets it may still reference this frame's locals, so time-out is fatal.
#define AWAIT_FLAG(flag)                                                  \
  do {                                                                    \
    auto deadline = std::chrono::steady_clock::now() + 10s;               \
    while (!(flag).load() && std::chrono::steady_clock::now() < deadline) \
      std::this_thread::sleep_for(1ms);                                   \
    if (!(flag).load()) {                                                 \
      std::fprintf(stderr, "FAIL: %s:%d: timed out on %s\n", __FILE__,    \
                   __LINE__, #flag);                                      \
      std::_Exit(1);                                                      \
    }                                                                     \
  } while (0)

// Starts a coroutine on the executor; done is set once it has finished.
void launch(RescheduleLazy<void> task, std::atomic<bool>& done) {
  std::move(task).start([&done](Try<void>&& t) {
    if (t.hasError()) {
      std::fprintf(stderr, "FAIL: a launched task threw\n");
      ++failures;
    }
    done = true;
  });
}

void allOk(const std::vector<Try<void>>& results) {
  for (const auto& r : results) {
    CHECK(!r.hasError());
  }
}

bool isInvalidArgument(const std::exception_ptr& e) {
  try {
    std::rethrow_exception(e);
  } catch (const std::invalid_argument& ex) {
    return std::string(ex.what()) == "odd";
  } catch (...) {
    return false;
  }
}

Lazy<int> fib(int n) {
  if (n < 2) {
    co_return n;
  }
  int a = co_await fib(n - 1);
  int b = co_await fib(n - 2);
  co_return a + b;
}

Lazy<int> half(int v) {
  if (v % 2 != 0) {
    throw std::invalid_argument("odd");
  }
  co_return v / 2;
}

Lazy<int> square(int v) { co_return v * v; }

void lazies() {
  CHECK(syncAwait(fib(12)) == 144);

  try {
    syncAwait(half(3));
    CHECK(!"half(3) did not throw");
  } catch (const std::invalid_argument& e) {
    CHECK(std::string(e.what()) == "odd");
  }

  auto tries = []() -> Lazy<int> {
    Try<int> bad = co_await half(5).coAwaitTry();
    Try<int> good = co_await half(8).coAwaitTry();
    co_return (bad.hasError() ? 100 : 0) + good.value();
  };
  CHECK(syncAwait(tries()) == 104);

  auto [even, odd] = syncAwait(collectAll(half(10), half(7)));
  CHECK(!even.hasError() && even.value() == 5);
  CHECK(odd.hasError() && isInvalidArgument(odd.getException()));
}

void executorsAndCollect(SimpleExecutor& ex) {
  auto where = [&ex]() -> Lazy<bool> {
    Executor* current = co_await CurrentExecutor{};
    co_return current == &ex && ex.currentThreadInExecutor();
  };
  CHECK(syncAwait(where().via(&ex)));
  CHECK(!ex.currentThreadInExecutor());

  std::vector<Lazy<int>> squares;
  for (int i = 0; i < 100; ++i) {
    squares.push_back(square(i));
  }
  auto all = syncAwait(collectAllPara(std::move(squares)).via(&ex));
  long sum = 0;
  for (auto& r : all) {
    sum += r.value();
  }
  CHECK(all.size() == 100 && sum == 328350);

  std::vector<RescheduleLazy<int>> racers;
  for (int i = 1; i <= 4; ++i) {
    racers.push_back(square(i).via(&ex));
  }
  auto first = syncAwait(collectAny(std::move(racers)));
  CHECK(first.index() < 4 && !first.hasError());
  CHECK(first.value() == int((first.index() + 1) * (first.index() + 1)));
}

void sleepAndCancel(SimpleExecutor& ex) {
  // collectAny gives each sleep a cancellation slot (see cabin.toml for
  // sleeps without one).  The 50 ms nap sleeps once the 30 s sleep has
  // started, wins, and Terminate cuts the 30 s sleep short.
  std::atomic<bool> armed{false};
  std::atomic<bool> cancelled{false};
  auto nap = [&]() -> Lazy<long long> {
    // SimpleExecutor queues tasks per thread without stealing, so yield
    // rather than block while the other task may wait behind this one.
    auto deadline = std::chrono::steady_clock::now() + 10s;
    while (!armed.load()) {
      if (std::chrono::steady_clock::now() > deadline) {
        std::fprintf(stderr, "FAIL: the 30 s sleep never started\n");
        std::_Exit(1);
      }
      co_await Yield{};
    }
    auto t0 = std::chrono::steady_clock::now();
    co_await async_simple::coro::sleep(50ms);
    co_return std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::steady_clock::now() - t0)
        .count();
  };
  auto longNap = [&]() -> Lazy<long long> {
    armed = true;
    Try<void> slept =
        co_await async_simple::coro::sleep(30s).coAwaitTry();
    if (slept.hasError()) {
      cancelled = true;
      co_return -1;
    }
    co_return 30000;
  };
  auto t0 = std::chrono::steady_clock::now();
  auto winner = syncAwait(collectAny<SignalType::Terminate>(
      nap().via(&ex), longNap().via(&ex)));
  CHECK(winner.index() == 0 && std::get<0>(winner).value() >= 40);
  AWAIT_FLAG(cancelled);
  CHECK(std::chrono::steady_clock::now() - t0 < 20s);
}

void mutexes(SimpleExecutor& ex) {
  Mutex mutex;
  long counter = 0;
  auto bump = [&]() -> Lazy<void> {
    for (int i = 0; i < 100; ++i) {
      auto lock = co_await mutex.coScopedLock();
      long seen = counter;
      std::this_thread::yield();
      counter = seen + 1;
    }
  };
  std::vector<Lazy<void>> bumps;
  for (int i = 0; i < 40; ++i) {
    bumps.push_back(bump());
  }
  allOk(syncAwait(collectAllPara(std::move(bumps)).via(&ex)));
  CHECK(counter == 4000);

  // A coroutine that wants a held mutex waits until it is unlocked.
  CHECK(mutex.tryLock());
  CHECK(!mutex.tryLock());
  std::atomic<bool> started{false};
  std::atomic<bool> took{false};
  auto take = [&]() -> Lazy<void> {
    started = true;
    co_await mutex.coLock();
    mutex.unlock();
  };
  launch(take().via(&ex), took);
  AWAIT_FLAG(started);
  std::this_thread::sleep_for(50ms);
  CHECK(!took);
  mutex.unlock();
  AWAIT_FLAG(took);
}

void sharedMutexes(SimpleExecutor& ex) {
  // SharedMutex's internal spin lock yields to the executor under
  // contention, so its coroutines run there.
  SharedMutex rw;
  CHECK(rw.tryLock());
  CHECK(!rw.tryLock());
  CHECK(!rw.tryLockShared());
  syncAwait(rw.unlock().via(&ex));
  CHECK(rw.tryLockShared());
  CHECK(rw.tryLockShared());
  CHECK(!rw.tryLock());
  syncAwait(rw.unlockShared().via(&ex));
  syncAwait(rw.unlockShared().via(&ex));

  // Readers never see a writer's half-done update.
  int x = 0;
  int y = 0;
  std::atomic<int> torn{0};
  std::atomic<int> reads{0};
  auto writer = [&]() -> Lazy<void> {
    co_await rw.coLock();
    ++x;
    std::this_thread::yield();
    ++y;
    co_await rw.unlock();
  };
  auto reader = [&]() -> Lazy<void> {
    co_await rw.coLockShared();
    if (x != y) {
      ++torn;
    }
    ++reads;
    co_await rw.unlockShared();
  };
  std::vector<Lazy<void>> jobs;
  for (int i = 0; i < 200; ++i) {
    jobs.push_back(i % 4 == 0 ? writer() : reader());
  }
  allOk(syncAwait(collectAllPara(std::move(jobs)).via(&ex)));
  CHECK(torn == 0 && reads == 150 && x == 50 && y == 50);

  // A reader waits while a writer holds the lock, a writer while a reader
  // does.
  CHECK(rw.tryLock());
  std::atomic<bool> readerStarted{false};
  std::atomic<bool> readerDone{false};
  auto lateReader = [&]() -> Lazy<void> {
    readerStarted = true;
    co_await rw.coLockShared();
    co_await rw.unlockShared();
  };
  launch(lateReader().via(&ex), readerDone);
  AWAIT_FLAG(readerStarted);
  std::this_thread::sleep_for(50ms);
  CHECK(!readerDone);
  syncAwait(rw.unlock().via(&ex));
  AWAIT_FLAG(readerDone);

  CHECK(rw.tryLockShared());
  std::atomic<bool> writerStarted{false};
  std::atomic<bool> writerDone{false};
  auto lateWriter = [&]() -> Lazy<void> {
    writerStarted = true;
    co_await rw.coLock();
    co_await rw.unlock();
  };
  launch(lateWriter().via(&ex), writerDone);
  AWAIT_FLAG(writerStarted);
  std::this_thread::sleep_for(50ms);
  CHECK(!writerDone);
  syncAwait(rw.unlockShared().via(&ex));
  AWAIT_FLAG(writerDone);
}

void latchesAndSemaphores(SimpleExecutor& ex) {
  Latch latch(8);
  std::atomic<int> arrived{0};
  std::atomic<int> seen{-1};
  std::atomic<bool> waiting{false};
  std::atomic<bool> released{false};
  auto waiter = [&]() -> Lazy<void> {
    waiting = true;
    co_await latch.wait();
    seen = arrived.load();
  };
  launch(waiter().via(&ex), released);
  AWAIT_FLAG(waiting);
  std::this_thread::sleep_for(20ms);
  CHECK(!released);
  CHECK(!syncAwait(latch.try_wait().via(&ex)));
  auto arrive = [&]() -> Lazy<void> {
    ++arrived;
    co_await latch.count_down();
  };
  std::vector<Lazy<void>> arrivals;
  for (int i = 0; i < 8; ++i) {
    arrivals.push_back(arrive());
  }
  allOk(syncAwait(collectAllPara(std::move(arrivals)).via(&ex)));
  AWAIT_FLAG(released);
  CHECK(seen == 8);
  CHECK(syncAwait(latch.try_wait().via(&ex)));

  CountingSemaphore<2> sem(2);
  std::atomic<int> inside{0};
  std::atomic<int> peak{0};
  auto guarded = [&]() -> Lazy<void> {
    co_await sem.acquire();
    int now = ++inside;
    int old = peak.load();
    while (now > old && !peak.compare_exchange_weak(old, now)) {
    }
    std::this_thread::sleep_for(2ms);
    --inside;
    co_await sem.release();
  };
  std::vector<Lazy<void>> users;
  for (int i = 0; i < 16; ++i) {
    users.push_back(guarded());
  }
  allOk(syncAwait(collectAllPara(std::move(users)).via(&ex)));
  CHECK(peak >= 1 && peak <= 2 && inside == 0);
  CHECK(syncAwait(sem.try_acquire().via(&ex)));
  CHECK(syncAwait(sem.try_acquire().via(&ex)));
  CHECK(!syncAwait(sem.try_acquire().via(&ex)));
  syncAwait(sem.release(2).via(&ex));
  CHECK(syncAwait(sem.try_acquire().via(&ex)));
}

void futures(SimpleExecutor& ex) {
  Promise<int> p;
  auto doubled =
      p.getFuture().via(&ex).thenValue([](int v) { return v * 2; });
  std::thread producer([&p] { p.setValue(21); });
  CHECK(std::move(doubled).get() == 42);
  producer.join();

  Promise<int> q;
  auto handled = q.getFuture().thenTry(
      [](Try<int>&& t) { return t.hasError() ? -1 : t.value(); });
  q.setException(std::make_exception_ptr(std::runtime_error("lost")));
  CHECK(std::move(handled).get() == -1);

  CHECK(async_simple::makeReadyFuture(7).get() == 7);

  // Results keep the input order whatever order the futures complete in.
  std::vector<Future<int>> parts;
  std::vector<Promise<int>> makers(3);
  for (auto& m : makers) {
    parts.push_back(m.getFuture());
  }
  auto gathered = async_simple::collectAll(parts.begin(), parts.end());
  for (int i = 2; i >= 0; --i) {
    makers[i].setValue(i + 1);
  }
  auto values = std::move(gathered).get();
  CHECK(values.size() == 3 && values[0].value() == 1 &&
        values[1].value() == 2 && values[2].value() == 3);

  // A Lazy suspends on a Future until another thread fulfils it.
  auto length = [](Future<std::string> f) -> Lazy<std::size_t> {
    std::string s = co_await std::move(f);
    co_return s.size();
  };
  Promise<std::string> sp;
  auto task = length(sp.getFuture());
  std::thread later([&sp] {
    std::this_thread::sleep_for(10ms);
    sp.setValue(std::string("async"));
  });
  CHECK(syncAwait(std::move(task).via(&ex)) == 5);
  later.join();
}

Generator<int> naturals() {
  for (int i = 0;; ++i) {
    co_yield i;
  }
}

Generator<int> countdown(int n) {
  if (n == 0) {
    co_return;
  }
  co_yield n;
  co_yield async_simple::ranges::elements_of(countdown(n - 1));
}

void generators() {
  int sum = 0;
  for (int v : naturals()) {
    if (v == 10) {
      break;
    }
    sum += v;
  }
  CHECK(sum == 45);

  std::vector<int> got;
  for (int v : countdown(5)) {
    got.push_back(v);
  }
  CHECK((got == std::vector<int>{5, 4, 3, 2, 1}));
}

}  // namespace

int main() {
  // Fail rather than hang if a lost wakeup blocks one of the waits.
  std::thread([] {
    std::this_thread::sleep_for(120s);
    std::fprintf(stderr, "FAIL: the test hung\n");
    std::_Exit(1);
  }).detach();

  SimpleExecutor ex(4);
  lazies();
  generators();
  executorsAndCollect(ex);
  sleepAndCancel(ex);
  mutexes(ex);
  sharedMutexes(ex);
  latchesAndSemaphores(ex);
  futures(ex);
  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures.load());
    return 1;
  }
  std::puts("async-simple: ok");
  return 0;
}
