#include <neko/event/event.hpp>

#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

int failures = 0;

#define CHECK(cond)                                                       \
  do {                                                                    \
    if (!(cond)) {                                                        \
      std::fprintf(stderr, "FAIL: %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      ++failures;                                                         \
    }                                                                     \
  } while (0)

using neko::Priority;
using neko::SyncMode;
using neko::event::EventLoop;

struct Msg {
  int id;
  std::string text;
};
struct Quit {};

struct EvenOnly : neko::event::EventFilter<int> {
  bool shouldProcess(const int& v) override { return v % 2 == 0; }
};

// Sync publish runs the handlers in the caller, in subscription order.
void sync_dispatch() {
  EventLoop loop;
  std::vector<std::string> log;
  auto a = loop.subscribe<Msg>(
      [&](const Msg& m) { log.push_back("a" + std::to_string(m.id) + m.text); });
  auto b = loop.subscribe<Msg>(
      [&](const Msg& m) { log.push_back("b" + std::to_string(m.id)); });
  CHECK(a == 1 && b == 2);

  loop.publish(Msg{7, "x"}, Priority::Normal, SyncMode::Sync);
  CHECK((log == std::vector<std::string>{"a7x", "b7"}));

  // A handler of another event type is not called.
  int ints = 0;
  loop.subscribe<int>([&](const int&) { ++ints; });
  loop.publish(Msg{8, ""}, Priority::Normal, SyncMode::Sync);
  CHECK(ints == 0 && log.size() == 4);

  // Unsubscribe removes exactly that handler; a repeat or wrong type fails.
  CHECK(loop.unsubscribe<Msg>(a));
  CHECK(!loop.unsubscribe<Msg>(a));
  CHECK(!loop.unsubscribe<int>(b));
  log.clear();
  loop.publish(Msg{9, ""}, Priority::Normal, SyncMode::Sync);
  CHECK((log == std::vector<std::string>{"b9"}));
}

// A handler's minimum priority drops lower-priority events.
void priority_filter() {
  EventLoop loop;
  std::vector<int> low, high;
  loop.subscribe<int>([&](const int& v) { low.push_back(v); });
  loop.subscribe<int>([&](const int& v) { high.push_back(v); },
                      Priority::High);
  loop.publish(1, Priority::Low, SyncMode::Sync);
  loop.publish(2, Priority::Normal, SyncMode::Sync);
  loop.publish(3, Priority::High, SyncMode::Sync);
  loop.publish(4, Priority::Critical, SyncMode::Sync);
  CHECK((low == std::vector<int>{1, 2, 3, 4}));
  CHECK((high == std::vector<int>{3, 4}));
}

// An EventFilter vetoes events for one handler.
void filters() {
  EventLoop loop;
  std::vector<int> seen, all;
  auto id = loop.subscribe<int>([&](const int& v) { seen.push_back(v); });
  loop.subscribe<int>([&](const int& v) { all.push_back(v); });
  CHECK(loop.addFilter<int>(id, std::make_unique<EvenOnly>()));
  CHECK(!loop.addFilter<int>(id + 100, std::make_unique<EvenOnly>()));
  for (int i = 1; i <= 4; ++i) loop.publish(i, Priority::Normal, SyncMode::Sync);
  CHECK((seen == std::vector<int>{2, 4}));
  CHECK((all == std::vector<int>{1, 2, 3, 4}));
}

// A handler's exception is logged and counted as failed, not processed, and later handlers still run.
void failing_handler() {
  EventLoop loop;
  std::vector<std::string> logs;
  loop.setLogger([&](const std::string& s) { logs.push_back(s); });
  int after = 0;
  loop.subscribe<int>([](const int&) { throw std::runtime_error("boom"); });
  loop.subscribe<int>([&](const int&) { ++after; });
  loop.publish(1, Priority::Normal, SyncMode::Sync);
  CHECK(after == 1);
  CHECK((logs == std::vector<std::string>{"Event handler failed: boom"}));
  auto s = loop.getStatistics();
  CHECK(s.totalEvents == 1 && s.processedEvents == 0 && s.failedEvents == 1);
}

// Async events, delayed events and tasks run inside run(); the watchdog
// bounds the wait if stopLoop is never reached.
void async_loop() {
  EventLoop loop;
  std::vector<std::string> log;
  loop.subscribe<Msg>(
      [&](const Msg& m) { log.push_back("msg" + std::to_string(m.id)); });
  // Each delayed event schedules the next, so their order never depends on
  // how long the test thread is paused.
  loop.subscribe<std::string>([&](const std::string& s) {
    log.push_back(s);
    if (s == "early") loop.publishAfter(50, std::string("late"));
    if (s == "late") loop.publishAfter(50, Quit{});
  });
  loop.subscribe<Quit>([&](const Quit&) { loop.stopLoop(); });

  loop.publish(Msg{1, ""});
  loop.publish(Msg{2, ""});
  CHECK(loop.getQueueSizes().eventQueueSize == 2);
  CHECK(log.empty());

  loop.publishAfter(50, std::string("early"));
  int ticks = 0;
  int stopped = 0;
  loop.scheduleRepeating(5, [&] { ++ticks; });
  loop.cancelTask(loop.scheduleRepeating(5, [&] { ++stopped; }));
  auto cancelled = loop.scheduleTask(30, [&] { log.push_back("never"); });
  CHECK(loop.cancelTask(cancelled));
  CHECK(!loop.cancelTask(cancelled));
  bool timedOut = false;
  loop.scheduleTask(5000, [&] {  // watchdog
    timedOut = true;
    loop.stopLoop();
  });

  CHECK(loop.isRunning());
  loop.run();
  CHECK(!timedOut && !loop.isRunning());
  CHECK((log == std::vector<std::string>{"msg1", "msg2", "early", "late"}));
  // Upstream limitation: scheduleRepeating runs its callback twice, then stops.
  CHECK(ticks == 2 && stopped == 0);
  CHECK(loop.getStatistics().failedEvents == 0);
}

}  // namespace

int main() {
  sync_dispatch();
  priority_filter();
  filters();
  failing_handler();
  async_loop();
  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("ok");
  return 0;
}
